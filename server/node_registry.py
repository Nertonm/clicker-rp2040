import time
import db
import asyncio

class NodeRegistry:
    def __init__(self, lamport_clock):
        self.nodes = {}
        self.lamport_clock = lamport_clock

    # Registra nós no banco de dados, e armazena valores em nodes, caso o nó exista apenas atualiza os valores.
    # Adicionalmente, registra relógio lamport para o nó.
    
    async def register_node(self, node_id, ip):
        now = time.time()
        if node_id not in self.nodes:
            self.nodes[node_id] = {
                "ip": ip,
                "last_seen": now,
                "status": "ACTIVE"
            } 
        else:
            self.nodes[node_id]["ip"] = ip
            self.nodes[node_id]["last_seen"] = now
            self.nodes[node_id]["status"] = "ACTIVE"
        await db.update_node(node_id, ip, "ACTIVE", last_seen)
        await self.lamport_clock.register(node_id)

    # Carrega valores armazenados no banco de dados em nodes.
    
    async def load_from_db(self):
        rows = await db.get_all_nodes()
        for row in rows:
            self.nodes[row["node_id"]] = {
                "ip": row["ip"],
                "last_seen": row["last_seen"] or 0,
                "status": row["status"]
            }

    # "heartbeat" que atualiza quando o nó foi "visto" pela última vez.

    async def heartbeat(self, node_id):
        if node_id in self.nodes:
            now = time.time()
            self.nodes[node_id]["last_seen"] = now
            await db.update_last_seen(node_id, now)

    # Checa quando o nó foi "visto" pela última vez, caso tenha se passado mais de 60 segundos, então atualiza status para INACTIVE.

    async def mark_inactive(self):
        now = time.time()
        for node_id, node in list(self.nodes.items()):
            if node["status"] != INACTIVE and (now - node["last_seen"]) > 60:
                node["status"] = INACTIVE
                await db.update_node(node_id, node["ip"], "INACTIVE")

    # Retorna nós em estado ACTIVE e SYNCING.

    async def get_active_nodes(self):
        return [node_id for node_id, node in self.nodes.items() if node["status"] in ("ACTIVE", "SYNCING")]

    # Atualiza status do nó, é usado pelo GameManager ao executar sync_offline.

    async def update_status(self, node_id, status):
        self.nodes[node_id]["status"] = status
        await db.update_node(node_id, self.nodes[node_id]["ip"], status)

    # Atualiza score do nó no banco de dados.

    async def update_score(self, node_id, local_score):
        await db.update_node_score(node_id, local_score) 

    # Retorna last_seen e score do nó, é usado pelo GameManager ao executar sync_offline. 

    async def get_node_last_seen_score(self, node_id):
        node = await db.get_node(node_id)
        if node is None:
            # Nó ainda não está no banco de dados; retorna valores padrão.
            return {
                "last_seen": 0,
                "score": 0,
            }
        response = {
            "last_seen": node.get("last_seen", 0),
            "score": node.get("local_score", 0),
        }
        return response

    # Consulta scores dos nós armazenados no banco de dados.

    async def get_nodes_scores(self):
        return await db.get_nodes_scores()

    # Loop que executa mark_inactive a cada 10 segundos.

    async def loop_detect_inactive(self):
        while True:
            await asyncio.sleep(10)
            await self.mark_inactive()

    # Insere evento no banco de dados.

    async def insert_event(self, node_id, sent, accepted, rate_limited, lamport_ts):
        rate_lim = 1 if rate_limited else 0
        await db.insert_event(node_id, sent, accepted, rate_lim, lamport_ts)
    
    # Insere milestone no banco de dados.

    async def insert_milestone(self, value, node_id, lamport_ts, now):
        await db.insert_milestone(value, node_id, lamport_ts, now)
            

