from infra import db
import time

class GameRepository:
    def __init__(self):
        pass

    async def update_score(self, node_id, local_score):
        """Atualiza score do nó no banco de dados."""
        await db.update_node_score(node_id, local_score)

    async def get_node_last_seen_score(self, node_id):
        """Retorna last_seen e score do nó para o GameManager."""
        node = await db.get_node(node_id)
        if node is None:
            return {"last_seen": 0, "score": 0}
        return {
            "last_seen": node.get("last_seen", 0),
            "score": node.get("local_score", 0),
        }

    async def get_nodes_scores(self):
        """Consulta scores de todos os nós."""
        return await db.get_nodes_scores()

    async def insert_event(self, node_id, sent, accepted, rate_limited, lamport_ts):
        """Persiste um evento de clique."""
        rate_lim = 1 if rate_limited else 0
        await db.insert_event(node_id, sent, accepted, rate_lim, lamport_ts)

    async def insert_milestone(self, value, node_id, lamport_ts, now):
        """Persiste a conquista de um milestone."""
        await db.insert_milestone(value, node_id, lamport_ts, now)
