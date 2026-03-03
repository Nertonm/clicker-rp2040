# Regras de Jogo no Servidor

Este documento descreve a lógica de jogo aplicada exclusivamente no backend: rate limiting por nó, milestones globais e power‑ups. O objetivo é garantir que nenhum cliente precise implementar regras de negócio.

## Visão geral

- Toda lógica de pontuação, limites e milestones vive no `GameManager`.
- Clientes enviam apenas cliques e timestamps lógicos (Lamport); o servidor decide o que aceitar, multiplicar ou rejeitar.
- A comunicação é feita via JSON‑RPC; as respostas sempre carregam o estado já calculado pelo servidor.

## Rate limiting por nó

### Estado em memória

O `GameManager` mantém, em memória, um dicionário por nó:

```python
self.nodes[node_id] = {
    "last_ts": float,        # último timestamp real (time.time()) em que o nó enviou um batch
    "score": int,            # score acumulado do nó
    "powerup_expire": float, # timestamp em que o power-up expira
    "multiplier": int,       # multiplicador de cliques (1 = normal, 3 = power-up)
}
```

O timestamp global de cliques é mantido em `self.global_score: int`.

### Cálculo de taxa

Para cada batch de cliques em `add_clicks(node_id, clicks, last_known_lamport_ts)`:

1. O servidor lê o tempo atual:
   ```python
   now = time.time()
   node = self._get_node_data(node_id)
   delta = now - node["last_ts"]
   node["last_ts"] = now
   ```

2. A taxa é inferida a partir de `delta` e do limite global:
   - **RATE_LIMIT = 50** cliques por segundo.
   - Se `delta > 0`, o número máximo de cliques permitidos é: `int(RATE_LIMIT * delta)`.
   - Se `delta <= 0` (timestamps iguais ou desordenados), é usada uma pequena janela sintética (por exemplo, equivalente a ~20 ms) para evitar aceitar uma explosão de cliques numa janela “zero”.
   - Em casos extremos de inatividade prolongada (por exemplo, mais de uma hora sem enviar nada), o servidor aceita o batch completo para não penalizar o primeiro envio após um longo intervalo.

### Corte parcial de cliques

Com `allowed` calculado:
```python
accepted = min(clicks, allowed) if allowed > 0 else 0
rejected = clicks - accepted
rate_exceeded = rejected > 0
```

- Se `accepted == 0`, o batch é totalmente bloqueado.
- Se `0 < accepted < clicks`, o servidor aplica corte parcial.

A resposta inclui sempre:
```json
{
  "status": "SUCCESS" ou "RATE_EXCEEDED",
  "accepted_clicks": <int>,
  "rejected_clicks": <int>,
  "global_score": <int>,
  "node_score": <int>,
  "lamport_ts": <int>,
  "milestone": <bool>,
  "milestone_value": <int|null>
}
```
Quando status é `"RATE_EXCEEDED"`, o cliente sabe quantos cliques foram efetivamente contabilizados.

## Milestones globais

### Marcos configurados

Os marcos globais de score são: `MILESTONES = [100, 500, 1000, 5000, 10000]`.

### Atualização de score e detecção de marcos

Depois de aplicar o rate limiting e o multiplicador de power-up, o `GameManager` atualiza os scores:
```python
before_global = self.global_score
node["score"] += actual_clicks  # accepted_clicks com multiplicador
self.global_score += actual_clicks
```

Em seguida, verifica quais marcos foram cruzados entre o estado anterior e o novo:
- Se um único batch fizer o score global pular, por exemplo, de 90 para 1200, todos os marcos intermediários que forem cruzados são registrados no banco.
- Para a resposta daquele batch, o servidor sinaliza apenas o marco mais alto atingido:
  ```python
  "milestone": len(milestones_reached) > 0,
  "milestone_value": milestones_reached[-1] if milestones_reached else None
  ```

Cada marco é sinalizado apenas uma vez, pois fica registrado em `self.milestones_done`.

## Power‑up e multiplicador

### Ativação

O método `activate_powerup(node_id)`:
- Ativa um power‑up de 10 segundos, com multiplicador 3x, caso expire o anterior.

### Aplicação no cálculo de cliques

Na hora de aplicar o score:
```python
actual_clicks = accepted
if node["powerup_expire"] > now:
    actual_clicks = accepted * node["multiplier"]
else:
    node["multiplier"] = 1
```
- O multiplicador só vale enquanto `powerup_expire > now`.
- Após o término, o multiplicador volta a 1 automaticamente.

## Sincronização offline (sync_offline)

O método `sync_offline(node_id, accumulated_clicks, last_known_lamport_ts)` serve para reconciliar cliques acumulados localmente enquanto o servidor esteve offline:
- Não aplica rate limiting, pois trata cliques históricos legítimos.
- Carrega estado do banco via `NodeRegistry` e atualiza o score do nó e o score global.
- Aplica a mesma lógica de milestones globais.
- Atualiza o status do nó (`SYNCING` → `ACTIVE`) e registra eventos/milestones no banco.

## Lamport clock e consistência causal

Antes de aplicar qualquer alteração de score, o `GameManager` valida o relógio de Lamport:
- Se `last_known_lamport_ts` recebido do cliente for menor ou igual ao último registrado, responde com `"error": "LAMPORT_VIOLATION"`.
- Isso garante que os eventos de cada nó sejam aplicados de forma causalmente consistente.

## Invariantes de arquitetura

- **Domínio isolado**: `GameManager`, `NodeRegistry` e `LamportClock` não importam `asyncio`, `json` ou `socket`.
- **Infra injetada**: locks (`asyncio.Lock`), transporte TCP e parsing JSON-RPC são responsabilidade de `rpc_server.py`.
- **Respostas estáveis**: as assinaturas de retorno são consistentes para facilitar o consumo pelo dispatcher e pelos clientes.
