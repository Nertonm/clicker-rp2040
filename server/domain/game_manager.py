import json
import time

from infra.logger import (
    log_normal, log_verbose, log_error, log_trace,
    metric_inc, get_metrics,
)

RATE_LIMIT = 50
MILESTONES = [100, 500, 1000, 5000, 10000]

class GameManager():
    def __init__(self, node_registry, game_repo, lamport_clock, lock=None, notifier=None):
        self.global_score = 0
        self.nodes = {} 
        self.milestones_done = set()
        self.node_registry = node_registry
        self.game_repo = game_repo
        self.lamport_clock = lamport_clock
        self.lock = lock
        self.notifier = notifier  # backward compat — sobrescrito por main.py
        self._event_notifier = None  # typed notifier — set via set_notifier()

    def set_notifier(self, fn):
        """Injeta callable tipado: async(event_type: str, payload_json: str)."""
        self._event_notifier = fn

    async def reconstruct_global_score(self):
        """Reconstrói global_score a partir dos scores persistidos no banco.
        Deve ser chamado no boot, após init_db(), para sobreviver a restarts."""
        scores = await self.game_repo.get_nodes_scores()
        self.global_score = sum(row.get("local_score", 0) for row in scores)
        log_normal("[GAME]", "global_score_reconstruido_do_banco",
                   global_score=self.global_score, nodes=len(scores))

    async def _notify(self, event_type: str, payload: dict):
        """Dispara evento para o dashboard. Nunca propaga excecoes ao caller."""
        if not self._event_notifier:
            return
        try:
            await self._event_notifier(event_type, json.dumps(payload))
        except Exception:
            pass

    def _get_node_data(self, node_id):
        if node_id not in self.nodes:
            self.nodes[node_id] = {
                "last_ts": 0,
                "score": 0,
                "powerup_expire": 0,
                "multiplier": 1
            }
        return self.nodes[node_id]

    def _check_score_invariant(self, context: str = ""):
        """Verifica que nenhum score individual é negativo."""
        for nid, nd in self.nodes.items():
            if nd["score"] < 0:
                log_error("[GAME]", "INVARIANTE: score negativo detectado",
                          context=context, node=nid, score=nd["score"])

    async def activate_powerup(self, node_id):
        now = time.time()
        node = self._get_node_data(node_id)
        
        if node["powerup_expire"] < now:
            node["powerup_expire"] = now + 10
            node["multiplier"] = 3
            log_normal("[GAME]", "powerup_ativado",
                       node=node_id, multiplier=3, expire_in_s=10)
            metric_inc("milestones_triggered")  # reutilizado como contador de powerups
            await self._notify("powerup_activated", {
                "node_id": node_id, "multiplier": 3, "duration_s": 10,
            })
            return {"status": "SUCCESS", "time_remaining": 10}
        
        remaining = round(node["powerup_expire"] - now, 1)
        log_verbose("[GAME]", "powerup_ja_ativo", node=node_id, remaining_s=remaining)
        return {"error": "ALREADY_ACTIVE", "remaining": remaining}

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
            metric_inc("lamport_violations")
            log_error("[LAMPORT]", "VIOLAÇÃO causal detectada",
                      node=node_id, received_ts=lamport_ts,
                      server_ts=last_ts, new_ts=curr_lamport,
                      total_violations=get_metrics().get("lamport_violations", "?"))
            return {"error": "LAMPORT_VIOLATION", "lamport_ts": curr_lamport}

        node = self._get_node_data(node_id)
        
        # Rate Limiting
        delta = now - node["last_ts"]
        node["last_ts"] = now
        
        if delta <= 0:
            allowed = RATE_LIMIT * 0.02 
        else:
            allowed = int(RATE_LIMIT * delta)
            
        accepted = min(clicks, allowed) if allowed > 0 else 0

        if delta > 3600:
            accepted = clicks

        rejected = clicks - accepted
        rate_exceeded = rejected > 0

        if rate_exceeded:
            metric_inc("rate_limited_clicks", rejected)
            log_normal("[GAME]", "rate_limit",
                       node=node_id, clicks_enviados=clicks,
                       aceitos=accepted, rejeitados=rejected,
                       delta_s=round(delta, 3), allowed=allowed)
        else:
            log_verbose("[GAME]", "clicks_aceitos",
                        node=node_id, clicks=accepted,
                        delta_s=round(delta, 3))

        # Aplicação de Multiplicador (Power-up)
        actual_clicks = accepted
        just_expired = (
            node["powerup_expire"] > 0 and
            node["powerup_expire"] <= now and
            node["multiplier"] > 1
        )
        if node["powerup_expire"] > now:
            actual_clicks = accepted * node["multiplier"]
            log_verbose("[GAME]", "powerup_aplicado",
                        node=node_id, aceitos=accepted,
                        multiplicador=node["multiplier"],
                        efetivo=actual_clicks)
        else:
            node["multiplier"] = 1
            if just_expired:
                await self._notify("powerup_expired", {"node_id": node_id})

        # Atualização de Scores
        before_node   = node["score"]
        before_global = self.global_score
        node["score"] += actual_clicks
        self.global_score += actual_clicks
        
        metric_inc("accepted_clicks", actual_clicks)

        log_verbose("[GAME]", "score_atualizado",
                    node=node_id,
                    score_node=f"{before_node}->{node['score']}",
                    global_score=f"{before_global}->{self.global_score}")

        await self.game_repo.update_score(node_id, node["score"])
        lamport_ts = await self.lamport_clock.update(node_id, lamport_ts)
        
        # Detecção de Milestones
        milestones_reached = []
        for m in MILESTONES:
            if m not in self.milestones_done and before_global < m <= self.global_score:
                self.milestones_done.add(m)
                milestones_reached.append(m)
                metric_inc("milestones_triggered")
                log_normal("[GAME]", "MILESTONE_ATINGIDO",
                           value=m, node=node_id,
                           global_score=self.global_score,
                           lamport_ts=lamport_ts)
                await self.game_repo.insert_milestone(m, node_id, lamport_ts, now)
                await self._notify("milestone", {
                    "node_id": node_id, "value": m,
                    "global_score": self.global_score,
                })

        await self.game_repo.insert_event(node_id, clicks, accepted, rate_exceeded, lamport_ts)
        await self.node_registry.heartbeat(node_id)

        # Invariante de sanidade
        self._check_score_invariant("add_clicks")

        # Notifica dashboard: batch de cliques processado com sucesso
        await self._notify("click_batch", {
            "node_id": node_id, "accepted": accepted,
            "global_score": self.global_score,
            "node_score": node["score"],
            "lamport_ts": lamport_ts,
        })

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
        
        log_normal("[SYNC]", "sync_offline_inicio",
                   node=node_id, accumulated=accumulated_clicks, lamport_ts=lamport_ts)

        # Atualiza status para SYNCING
        await self.node_registry.load_from_db()
        await self.node_registry.update_status(node_id, "SYNCING")
        
        # Carrega estado do nó
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
            log_normal("[SYNC]", "primeira_sync_aceita_tudo",
                       node=node_id, accepted=accepted)
        else:
            offline_seconds = max(0, now - last_seen)
            # Limite máximo: RATE_LIMIT clicks/s × tempo offline
            max_allowed = int(RATE_LIMIT * offline_seconds)

            # Aceita apenas o mínimo entre cliques enviados e limite calculado
            accepted = min(accumulated_clicks, max_allowed)
            rejected = accumulated_clicks - accepted

            if rejected > 0:
                metric_inc("rate_limited_clicks", rejected)
                log_normal("[SYNC]", "rate_limit_sync",
                           node=node_id,
                           accumulated=accumulated_clicks,
                           accepted=accepted, rejected=rejected,
                           offline_s=round(offline_seconds, 1),
                           max_allowed=max_allowed)
            else:
                log_normal("[SYNC]", "sync_aceita_tudo",
                           node=node_id, accepted=accepted,
                           offline_s=round(offline_seconds, 1))
        
        # ============================================================
        # FIM DO RATE LIMITING
        # ============================================================
        
        # Atualização de scores
        before_global = self.global_score
        node["score"] += accepted
        self.global_score += accepted
        metric_inc("accepted_clicks", accepted)

        log_verbose("[SYNC]", "score_pos_sync",
                    node=node_id,
                    score_node=node["score"],
                    global_antes=before_global,
                    global_depois=self.global_score)

        await self.game_repo.update_score(node_id, node["score"])
        lamport_ts = await self.lamport_clock.update(node_id, lamport_ts)
        
        # Detecção de milestones
        milestones_reached = []
        for m in MILESTONES:
            if m not in self.milestones_done and before_global < m <= self.global_score:
                self.milestones_done.add(m)
                milestones_reached.append(m)
                metric_inc("milestones_triggered")
                log_normal("[SYNC]", "MILESTONE_SYNC",
                           value=m, node=node_id,
                           global_score=self.global_score)
                await self.game_repo.insert_milestone(m, node_id, lamport_ts, now)
                await self._notify("milestone", {
                    "node_id": node_id, "value": m,
                    "global_score": self.global_score,
                })

        await self.game_repo.insert_event(
            node_id, 
            accumulated_clicks,
            accepted,
            rejected > 0,
            lamport_ts
        )
        
        # Invariante de sanidade
        self._check_score_invariant("sync_offline")

        await self.node_registry.update_status(node_id, "ACTIVE")
        await self.node_registry.heartbeat(node_id)
        node["last_ts"] = now

        log_normal("[SYNC]", "sync_offline_ok",
                   node=node_id, accepted=accepted, rejected=rejected,
                   global_score=self.global_score,
                   node_score=node["score"], lamport_ts=lamport_ts)

        # Notifica o dashboard da transição SYNCING -> ACTIVE
        await self._notify("sync_complete", {
            "node_id": node_id,
            "transition": "SYNCING->ACTIVE",
            "global_score": self.global_score,
            "node_score": node["score"],
            "lamport_ts": lamport_ts,
        })

        # Coleta scores de todos os nós para compor a resposta completa
        node_scores = await self.game_repo.get_nodes_scores()

        return {
            "status": "SUCCESS",
            "accepted_clicks": accepted,
            "rejected_clicks": rejected,
            "global_score": self.global_score,
            "local_score": node["score"],
            "node_score": node["score"],
            "lamport_ts": lamport_ts,
            "milestone": len(milestones_reached) > 0,
            "milestone_value": milestones_reached[-1] if milestones_reached else None,
            "node_scores": node_scores,
        }

