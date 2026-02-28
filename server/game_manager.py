import asyncio
import time

RATE_LIMIT = 50
MILESTONES = [100, 500, 1000, 5000, 10000]

class GameManager():
    def __init__(self, node_registry, lamport_clock):
        self.lock = asyncio.Lock()
        self.global_score = 0
        self.nodes = {}
        self.milestones_done = set()
        self.node_registry = node_registry
        self.lamport_clock = lamport_clock

    # Ativa power-up, caso já não esteja ativo.

    def activate_powerup(self, node_id):
        now = time.time()
        if node_id not in self.nodes:
            self.nodes[node_id] = {
                "last_ts": now,
                "score": 0,
                "powerup_expire": 0,
                "powerup_active": False
            }
        if not self.nodes[node_id]["powerup_active"]:
            self.nodes[node_id]["powerup_expire"] = now
            self.nodes[node_id]["powerup_active"] = True
            response = {
                "status": "SUCCESS",
                "time_remaining": 10
            }
            return response
        response = {
            "error": "ALREADY_ACTIVE"
        }
        return response

    # Adiciona clicks ao score.

    async def add_clicks(self, node_id, clicks, last_known_lamport_ts):
        async with self.lock:
            now = time.time()
            last = await self.lamport_clock.get_last_by_node(node_id)
            # Checa se lamport do nó não viola o relógio global. Caso sim, retorna erro e lamport correto.
            if last_known_lamport_ts <= last:
                lamport_ts = await self.lamport_clock.update(node_id, last_known_lamport_ts)
                response = {
                    "error": "LAMPORT_VIOLATION",
                    "lamport_ts": lamport_ts
                }
                return response
            if node_id not in self.nodes:
                self.nodes[node_id] = {
                    "last_ts": now,
                    "score": 0,
                    "powerup_expire": 0,
                    "powerup_active": False
                }
            # Se power-up estiver ativo, cliques são multiplicados por 3. Power-up é desativado caso tenha se passado 10 segundos.
            if self.nodes[node_id]["powerup_active"] and (now - self.nodes[node_id]["powerup_expire"]) < 10:
                clicks = clicks * 3
            else:
                self.nodes[node_id]["powerup_active"] = False
            # Existe um limite de cliques por segundo, caso a taxa de cliques enviados ultrapasse-o é necessário limitar cliques aceitos.
            delta = now - self.nodes[node_id]["last_ts"]
            rate = int(clicks / delta) if delta > 0 else clicks
            allowed = int(RATE_LIMIT * delta) if delta > 0 else RATE_LIMIT
            accepted = clicks if rate <= RATE_LIMIT else allowed
            rate_exceeded = rate > RATE_LIMIT
            self.nodes[node_id]["score"] += accepted
            await self.node_registry.update_score(node_id, self.nodes[node_id]["score"])
            self.nodes[node_id]["last_ts"] = now
            self.global_score += accepted
            # Atualiza lamport do nó.
            lamport_ts = await self.lamport_clock.update(node_id, last_known_lamport_ts)
            milestone = None
            # Checa se algum milestone foi atingido.
            for m in MILESTONES:
                if self.global_score >= m and m not in self.milestones_done:
                    self.milestones_done.add(m)
                    milestone = m
                    break
            # Registra evento.
            await self.node_registry.insert_event(node_id, clicks, accepted, rate_exceeded, lamport_ts)
            # Resposta padrão.
            response = {
                "clicks": clicks,
                "global_score": self.global_score,
                "node_score": self.nodes[node_id]["score"],
                "lamport_ts": lamport_ts
            }
            if rate_exceeded:
                # Adicional caso a taxa ultrapasse limite.
                response["error"] = "RATE_EXCEEDED"
                response["accepted_partial"] = accepted
            if milestone:
                # Adicional caso milestone seja alcançado.
                response["milestone"] = True
                response["milestone_value"] = milestone
                # Registra milestone.
                await self.node_registry.insert_milestone(milestone, node_id, lamport_ts, now)
            return response

    # Sincroniza cliques acumulados com servidor offline.

    async def sync_offline(self, node_id, accumulated_clicks, last_known_lamport_ts):
        async with self.lock:
            now = time.time()
            # NodeRegistry recupera dados do banco de dados.
            await self.node_registry.load_from_db()
            # Atualiza status do nó.
            await self.node_registry.update_status(node_id, "SYNCING")
            # Recupera últimos score e last_seen salvos do nó.
            node = await self.node_registry.get_node_last_seen_score(node_id)
            if node_id not in self.nodes:
                self.nodes[node_id] = {
                    "last_ts": node["last_seen"],
                    "score": node["score"],
                    "powerup_expire": 0,
                    "powerup_active": False
                }
            # Mesmo cálculo de cliques permitidos.
            delta = now - node["last_seen"]
            rate = int(accumulated_clicks / delta) if delta > 0 else accumulated_clicks
            allowed = int(RATE_LIMIT * delta) if delta > 0 else RATE_LIMIT
            accepted = accumulated_clicks if rate <= RATE_LIMIT else allowed
            rate_exceeded = rate > RATE_LIMIT
            self.nodes[node_id]["score"] += accepted
            await self.node_registry.update_score(node_id, accepted)
            self.nodes[node_id]["last_ts"] = now
            self.global_score += accepted
            # Relógio lamport global é atualizado com base no último lamport visto do nó.
            lamport_ts = await self.lamport_clock.update(node_id, last_known_lamport_ts)
            # Registra evento.
            await self.node_registry.insert_event(node_id, accumulated_clicks, accepted, rate_exceeded, lamport_ts)
            milestone = None
            for m in MILESTONES:
                if self.global_score >= m and m not in self.milestones_done:
                    milestone = m
                    self.milestones_done.add(m)
                    # Registra milestone.
                    await self.node_registry.insert_milestone(m, node_id, lamport_ts, now)
                    break
            await self.node_registry.update_status(node_id, "ACTIVE")
            #Respostas padrões.
            response = {
                "clicks": accumulated_clicks,
                "global_score": self.global_score,
                "node_score": self.nodes[node_id]["score"],
                "lamport_ts": lamport_ts
            }
            if rate_exceeded:
                response["error"] = "RATE_EXCEEDED"
                response["accepted_partial"] = accepted
            if milestone:
                response["milestone"] = True
                response["milestone_value"] = milestone
            return response


