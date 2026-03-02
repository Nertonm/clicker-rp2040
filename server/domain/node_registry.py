import time
from infra import db
from infra.logger import log_normal, log_verbose, log_error, log_trace, metric_inc

ACTIVE   = "ACTIVE"
SYNCING  = "SYNCING"
INACTIVE = "INACTIVE"

class NodeRegistry:
    def __init__(self, lamport_clock):
        self.nodes = {}
        self.lamport_clock = lamport_clock

    async def register_node(self, node_id, ip):
        """Registra ou atualiza um nó (idempotente)."""
        now = time.time()
        is_new = node_id not in self.nodes
        # UPSERT na memória e no banco
        self.nodes[node_id] = {
            "ip": ip,
            "last_seen": now,
            "status": ACTIVE
        }
        await db.update_node(node_id, ip, ACTIVE, now)
        await self.lamport_clock.register(node_id)

        metric_inc("nodes_registered")
        if is_new:
            log_normal("[NODE]", "register_novo",
                       node=node_id, ip=ip, total_registered=len(self.nodes))
        else:
            log_verbose("[NODE]", "register_reativacao",
                        node=node_id, ip=ip)

    async def load_from_db(self):
        """Sincroniza o estado em memória com o banco de dados."""
        rows = await db.get_all_nodes()
        for row in rows:
            self.nodes[row["node_id"]] = {
                "ip": row["ip"],
                "last_seen": row["last_seen"] or 0,
                "status": row["status"]
            }
        log_trace("[NODE]", "load_from_db", count=len(rows))

    async def heartbeat(self, node_id):
        """Atualiza o timestamp de última atividade do nó."""
        if node_id in self.nodes:
            now = time.time()
            prev_status = self.nodes[node_id]["status"]
            self.nodes[node_id]["last_seen"] = now
            # Reativar nó se estava INACTIVE
            if prev_status == INACTIVE:
                self.nodes[node_id]["status"] = ACTIVE
                await db.update_node(node_id, self.nodes[node_id]["ip"], ACTIVE, now)
                log_normal("[NODE]", "REATIVADO_via_heartbeat",
                           node=node_id, prev_status=prev_status)
            else:
                await db.update_last_seen(node_id, now)
                log_trace("[NODE]", "heartbeat", node=node_id)
        else:
            log_error("[NODE]", "heartbeat para nó desconhecido", node=node_id)

    async def mark_inactive(self):
        """Job: Marca como INACTIVE nós sem heartbeat há mais de 60s."""
        now = time.time()
        for node_id, node in list(self.nodes.items()):
            if node["status"] != INACTIVE and (now - node["last_seen"]) > 60:
                idle_s = round(now - node["last_seen"], 1)
                node["status"] = INACTIVE
                await db.update_node(node_id, node["ip"], INACTIVE, node["last_seen"])
                metric_inc("nodes_marked_inactive")
                log_normal("[NODE]", "INATIVO",
                           node=node_id, idle_s=idle_s)

    async def get_active_nodes(self):
        """Retorna lista de nós online (ACTIVE ou SYNCING)."""
        active = [
            {
                "node_id": node_id,
                "ip": node["ip"],
                "status": node["status"],
                "last_seen": node["last_seen"]
            }
            for node_id, node in self.nodes.items()
            if node["status"] in (ACTIVE, SYNCING)
        ]
        log_trace("[NODE]", "get_active_nodes", count=len(active))
        return active

    async def update_status(self, node_id, status):
        """Altera o status de presença de um nó."""
        if node_id not in self.nodes:
            node = await db.get_node(node_id)
            if not node:
                log_error("[NODE]", "update_status: nó não encontrado", node=node_id)
                return
            self.nodes[node_id] = {
                "ip": node["ip"],
                "last_seen": node.get("last_seen") or 0,
                "status": node["status"],
            }

        prev = self.nodes[node_id]["status"]
        self.nodes[node_id]["status"] = status
        await db.update_node_status(node_id, status)

        if prev != status:
            log_normal("[NODE]", "status_change",
                       node=node_id, prev=prev, new=status)

            

