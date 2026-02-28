import time
import db

ACTIVE = "ACTIVE"
SYNCING = "SYNCING"
INACTIVE = "INACTIVE"

class NodeRegistry:
    def __init__(self, lamport_clock):
        self.nodes = {}
        self.lamport_clock = lamport_clock

    async def register_node(self, node_id, ip):
        """Registra ou atualiza um nó (idempotente)."""
        now = time.time()
        # UPSERT na memória e no banco
        self.nodes[node_id] = {
            "ip": ip,
            "last_seen": now,
            "status": ACTIVE
        }
        await db.update_node(node_id, ip, ACTIVE, now)
        await self.lamport_clock.register(node_id)

    async def load_from_db(self):
        """Sincroniza o estado em memória com o banco de dados."""
        rows = await db.get_all_nodes()
        for row in rows:
            self.nodes[row["node_id"]] = {
                "ip": row["ip"],
                "last_seen": row["last_seen"] or 0,
                "status": row["status"]
            }

    async def heartbeat(self, node_id):
        """Atualiza o timestamp de última atividade do nó."""
        if node_id in self.nodes:
            now = time.time()
            self.nodes[node_id]["last_seen"] = now
            # Opcional: Reativar nó se estava INACTIVE
            if self.nodes[node_id]["status"] == INACTIVE:
                self.nodes[node_id]["status"] = ACTIVE
                await db.update_node(node_id, self.nodes[node_id]["ip"], ACTIVE, now)
            else:
                await db.update_last_seen(node_id, now)

    async def mark_inactive(self):
        """Job: Marca como INACTIVE nós sem heartbeat há mais de 60s."""
        now = time.time()
        for node_id, node in list(self.nodes.items()):
            if node["status"] != INACTIVE and (now - node["last_seen"]) > 60:
                node["status"] = INACTIVE
                # Fix: update_node espera (node_id, ip, status, last_seen)
                await db.update_node(node_id, node["ip"], INACTIVE, node["last_seen"])
                print(f"[NodeRegistry] Nó {node_id} marcado como INACTIVE (timeout)")

    async def get_active_nodes(self):
        """Retorna lista de nós online (ACTIVE ou SYNCING)."""
        return [
            {
                "node_id": node_id,
                "ip": node["ip"],
                "status": node["status"],
                "last_seen": node["last_seen"]
            }
            for node_id, node in self.nodes.items()
            if node["status"] in (ACTIVE, SYNCING)
        ]

    async def update_status(self, node_id, status):
        """Altera o status de presença de um nó."""
        if node_id not in self.nodes:
            node = await db.get_node(node_id)
            if not node:
                return
            self.nodes[node_id] = {
                "ip": node["ip"],
                "last_seen": node.get("last_seen") or 0,
                "status": node["status"],
            }
        
        self.nodes[node_id]["status"] = status
        await db.update_node_status(node_id, status)
            

