import time

RATE_LIMIT = 50
MILESTONES = [100, 500, 1000, 5000, 10000]

class GameManager():
    def __init__(self, node_registry, game_repo, lamport_clock, lock=None):
        self.global_score = 0
        self.nodes = {} 
        self.milestones_done = set()
        self.node_registry = node_registry
        self.game_repo = game_repo
        self.lamport_clock = lamport_clock
        self.lock = lock

    def _get_node_data(self, node_id):
        if node_id not in self.nodes:
            self.nodes[node_id] = {
                "last_ts": 0,
                "score": 0,
                "powerup_expire": 0,
                "multiplier": 1
            }
        return self.nodes[node_id]

    def activate_powerup(self, node_id):
        now = time.time()
        node = self._get_node_data(node_id)
        
        if node["powerup_expire"] < now:
            node["powerup_expire"] = now + 10
            node["multiplier"] = 3
            return {"status": "SUCCESS", "time_remaining": 10}
        
        return {"error": "ALREADY_ACTIVE"}

    async def add_clicks(self, node_id, clicks, lamport_ts):
        if self.lock:
            async with self.lock:
                return await self._add_clicks_logic(node_id, clicks, lamport_ts)
        return await self._add_clicks_logic(node_id, clicks, lamport_ts)

    async def _add_clicks_logic(self, node_id, clicks, lamport_ts):
        now = time.time()
        
        # Sincronização causal (Lamport)
        last_ts = await self.lamport_clock.get_last_by_node(node_id)
        if lamport_ts <= last_ts:
            curr_lamport = await self.lamport_clock.update(node_id, lamport_ts)
            # Persiste a violação para auditoria
            await self.game_repo.insert_lamport_violation(
                node_id, 
                received_ts=lamport_ts, 
                server_ts=last_ts
            )
            return {"error": "LAMPORT_VIOLATION", "lamport_ts": curr_lamport}

        node = self._get_node_data(node_id)
        
        # Rate Limiting
        delta = now - node["last_ts"]
        node["last_ts"] = now
        
        # Delta <= 0: Ocorre em batidas de timestamp idênticas ou muito próximas.
        # Aplicamos uma janela mínima (20ms) para não bloquear completamente o tráfego legítimo
        # mas ainda assim desencorajar spam no mesmo milissegundo.
        if delta <= 0:
            allowed = RATE_LIMIT * 0.02 
        else:
            allowed = int(RATE_LIMIT * delta)
            
        accepted = min(clicks, allowed) if allowed > 0 else 0

        # Delta > 3600 (1 hora): Heurística para "Reset" ou primeira conexão após longo tempo.
        # Evita que um nó que ficou offline seja penalizado injustamente ou gere overflow
        # no primeiro batch de reconexão.
        if delta > 3600:
            accepted = clicks

        rejected = clicks - accepted
        rate_exceeded = rejected > 0

        # Aplicação de Multiplicador (Power-up)
        actual_clicks = accepted
        if node["powerup_expire"] > now:
            actual_clicks = accepted * node["multiplier"]
        else:
            node["multiplier"] = 1

        # Atualização de Scores
        before_global = self.global_score
        node["score"] += actual_clicks
        self.global_score += actual_clicks
        
        await self.game_repo.update_score(node_id, node["score"])
        lamport_ts = await self.lamport_clock.update(node_id, lamport_ts)
        
        # Detecção de Milestones (Pode cruzar múltiplos marcos)
        milestones_reached = []
        for m in MILESTONES:
            if m not in self.milestones_done and before_global < m <= self.global_score:
                self.milestones_done.add(m)
                milestones_reached.append(m)
                await self.game_repo.insert_milestone(m, node_id, lamport_ts, now)

        await self.game_repo.insert_event(node_id, clicks, accepted, rate_exceeded, lamport_ts)
        await self.node_registry.heartbeat(node_id)

        response = {
            "status": "RATE_EXCEEDED" if rate_exceeded else "SUCCESS",
            "accepted_clicks": accepted,
            "rejected_clicks": rejected,
            "global_score": self.global_score,
            "node_score": node["score"],
            "lamport_ts": lamport_ts,
            "milestone": len(milestones_reached) > 0,
            "milestone_value": milestones_reached[-1] if milestones_reached else None
        }
        return response

    async def sync_offline(self, node_id, accumulated_clicks, lamport_ts):
        if self.lock:
            async with self.lock:
                return await self._sync_offline_logic(node_id, accumulated_clicks, lamport_ts)
        return await self._sync_offline_logic(node_id, accumulated_clicks, lamport_ts)

    async def _sync_offline_logic(self, node_id, accumulated_clicks, lamport_ts):
        """
        Reconciliação de cliques acumulados durante período offline.
        Aplica rate limiting proporcional ao tempo de ausência: 50 clicks/s × tempo_offline.
        """
        now = time.time()
        
        # Atualiza status para SYNCING (visível no dashboard)
        await self.node_registry.load_from_db()
        await self.node_registry.update_status(node_id, "SYNCING")
        
        # Carrega estado do nó do banco de dados
        db_node = await self.game_repo.get_node_last_seen_score(node_id)
        node = self._get_node_data(node_id)
        node["score"] = db_node["score"]
        
        # ============================================================
        # RATE LIMITING PROPORCIONAL AO TEMPO OFFLINE
        # ============================================================
        
        # Calcula tempo offline baseado em last_seen (timestamp Unix)
        last_seen = db_node.get("last_seen")
        
        if last_seen is None:
            # Primeira sincronização deste nó: aceita tudo
            accepted = accumulated_clicks
            rejected = 0
            print(f"[SYNC] Nó {node_id}: primeira sincronização, aceitando {accumulated_clicks} cliques")
        else:
            offline_seconds = max(0, now - last_seen)
            # Limite máximo: RATE_LIMIT clicks/s × tempo offline
            max_allowed = int(RATE_LIMIT * offline_seconds)
            
            # Aceita apenas o mínimo entre cliques enviados e limite calculado
            accepted = min(accumulated_clicks, max_allowed)
            rejected = accumulated_clicks - accepted
            
            # Log detalhado de rate limiting
            if rejected > 0:
                print(f"[SYNC] Nó {node_id}: {accumulated_clicks} enviados, "
                      f"{accepted} aceitos, {rejected} descartados "
                      f"(offline por {offline_seconds:.1f}s, limite={max_allowed})")
            else:
                print(f"[SYNC] Nó {node_id}: {accepted} cliques aceitos "
                      f"(offline por {offline_seconds:.1f}s)")
        
        # ============================================================
        # FIM DO RATE LIMITING
        # ============================================================
        
        # Atualização de scores e milestones (lógica existente)
        before_global = self.global_score
        node["score"] += accepted
        self.global_score += accepted
        
        await self.game_repo.update_score(node_id, node["score"])
        lamport_ts = await self.lamport_clock.update(node_id, lamport_ts)
        
        # Detecção de milestones
        milestones_reached = []
        for m in MILESTONES:
            if m not in self.milestones_done and before_global < m <= self.global_score:
                self.milestones_done.add(m)
                milestones_reached.append(m)
                await self.game_repo.insert_milestone(m, node_id, lamport_ts, now)

        # Persiste evento (rate_exceeded=True se houve rejeição)
        await self.game_repo.insert_event(
            node_id, 
            accumulated_clicks,  # Total enviado
            accepted,            # Total aceito
            rejected > 0,        # Flag rate_exceeded
            lamport_ts
        )
        
        # Volta para status ACTIVE
        await self.node_registry.update_status(node_id, "ACTIVE")
        await self.node_registry.heartbeat(node_id) # Persiste last_seen no banco
        node["last_ts"] = now

        # Retorna estado completo
        return {
            "status": "SUCCESS",
            "accepted_clicks": accepted,
            "rejected_clicks": rejected,
            "global_score": self.global_score,
            "node_score": node["score"],
            "lamport_ts": lamport_ts,
            "milestone": len(milestones_reached) > 0,
            "milestone_value": milestones_reached[-1] if milestones_reached else None
        }


