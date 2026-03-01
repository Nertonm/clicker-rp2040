from infra import db

class DashboardStateService:
    def __init__(self, game_manager, node_registry, get_active_connections_fn):
        self.game_manager = game_manager
        self.node_registry = node_registry
        self.get_active_connections = get_active_connections_fn

    async def collect_state(self):
        """Coleta o estado atual do sistema para exibição no dashboard."""
        global_score = self.game_manager.global_score if self.game_manager else 0
        active_conns = self.get_active_connections() if self.get_active_connections else 0

        # Nós com status e score
        nodes = []
        if self.node_registry:
            all_nodes = await db.get_all_nodes()
            for n in all_nodes:
                nodes.append({
                    "node_id": n.get("node_id"),
                    "status": n.get("status"),
                    "score": n.get("local_score", 0),
                    "last_seen": n.get("last_seen")
                })

        # Últimos 20 eventos
        events = await db.get_recent_events(limit=20)

        return {
            "global_score": global_score,
            "nodes": nodes,
            "events": events,
            "active_connections": active_conns
        }
