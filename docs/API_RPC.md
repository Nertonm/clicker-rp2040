# API_RPC.md - Referência da API JSON-RPC 2.0

Todos os metodos sao invocados sobre uma conexao TCP persistente na porta 8765. O protocolo e JSON-RPC 2.0: cada requisicao e uma linha JSON terminada em `\n`; cada resposta e uma linha JSON terminada em `\n`.

---

## Formato base

```json
Requisicao:  {"jsonrpc":"2.0","method":"<nome>","params":{...},"id":<int>}
Sucesso:     {"jsonrpc":"2.0","result":{...},"id":<int>}
Erro:        {"jsonrpc":"2.0","error":{"code":<int>,"message":"<str>"},"id":<int>}
```

---

## Metodo: `register_node`

**Descricao:** Registra ou reativa um no. Idempotente, pode ser chamado repetidamente como heartbeat.

**Parametros:**

| Campo | Tipo | Obrigatorio | Descricao |
|-------|------|------------|-----------|
| `node_id` | integer | sim | Identificador do no (0, 1 ou 2) |
| `ip` | string | nao (injetado pelo servidor) | IP do cliente; injetado automaticamente pelo dispatcher com o IP da conexao TCP |

**Retorno:** `null` (campo `result` presente e `None` em Python, que vira `null` no JSON)

**Erros possíveis:**

| Codigo | Condicao |
|--------|---------|
| -32600 | Request invalido (nao e dict ou `jsonrpc != "2.0"`) |
| -32000 | Excecao interna do servidor |

**Exemplo:**

Requisicao (firmware envia com `id=2`):
```json
{"jsonrpc":"2.0","method":"register_node","params":{"node_id":0},"id":2}
```

Resposta de sucesso:
```json
{"jsonrpc":"2.0","result":null,"id":2}
```

---

## Metodo: `heartbeat`

**Descricao:** Atualiza o timestamp `last_seen` do no para evitar que seja marcado como INACTIVE.

**Parametros:**

| Campo | Tipo | Obrigatorio | Descricao |
|-------|------|------------|-----------|
| `node_id` | integer | sim | Identificador do no |

**Retorno:** `null`

**Erros possíveis:**

| Codigo | Condicao |
|--------|---------|
| -32000 | No desconhecido (log de erro, mas nao propaga excecao normalmente) |

**Exemplo:**

```json
{"jsonrpc":"2.0","method":"heartbeat","params":{"node_id":1},"id":10}
```
```json
{"jsonrpc":"2.0","result":null,"id":10}
```

**Nota:** O firmware (`task_rpc.c`) nao usa este metodo diretamente; usa `register_node` tanto no registro inicial quanto no heartbeat periodico (a cada 30s).

---

## Metodo: `add_clicks`

**Descricao:** Envia um lote de cliques do no para o servidor. Aplica rate limiting, multiplicador de power-up, deteccao de milestones e validacao causal (Lamport).

**Parametros:**

| Campo | Tipo | Obrigatorio | Descricao |
|-------|------|------------|-----------|
| `node_id` | integer | sim | Identificador do no |
| `clicks` | integer | sim | Quantidade de cliques no lote |
| `lamport_ts` | integer | sim | Timestamp logico de Lamport (incrementado antes do envio) |

**Retorno (sucesso):**

| Campo | Tipo | Descricao |
|-------|------|-----------|
| `status` | string | `"SUCCESS"` ou `"RATE_EXCEEDED"` |
| `accepted_clicks` | integer | Cliques efetivamente aceitos pelo servidor |
| `rejected_clicks` | integer | Cliques rejeitados pelo rate limiting |
| `global_score` | integer | Pontuacao global atual (todos os nos) |
| `node_score` | integer | Pontuacao local atual do no |
| `lamport_ts` | integer | Timestamp Lamport atualizado pelo servidor |
| `milestone` | boolean | `true` se um marco foi atingido neste lote |
| `milestone_value` | integer ou null | Valor do marco atingido (ex: 1000) ou `null` |

**Erros possíveis:**

| Codigo de aplicacao | Campo no JSON de erro | Condicao |
|--------------------|-----------------------|---------|
| - | `"error": "LAMPORT_VIOLATION"` | `lamport_ts <= last_lamport` do no no servidor |
| - | `"error": "RATE_EXCEEDED"` | Mais cliques enviados do que o rate limit permite (retornado dentro de `result`, nao de `error` JSON-RPC) |

**Nota sobre RATE_EXCEEDED:** o servidor retorna este caso dentro do campo `result` (nao como erro JSON-RPC), com `status: "RATE_EXCEEDED"`. O firmware detecta via `json_contains(json, "RATE_EXCEEDED")`.

**Exemplo - sucesso:**

Requisicao:
```json
{"jsonrpc":"2.0","method":"add_clicks","params":{"node_id":0,"clicks":5,"lamport_ts":7},"id":1}
```

Resposta:
```json
{"jsonrpc":"2.0","result":{"status":"SUCCESS","accepted_clicks":5,"rejected_clicks":0,"global_score":105,"node_score":55,"lamport_ts":8,"milestone":false,"milestone_value":null},"id":1}
```

**Exemplo - LAMPORT_VIOLATION:**

Resposta (servidor detectou timestamp regressivo):
```json
{"jsonrpc":"2.0","result":{"error":"LAMPORT_VIOLATION","lamport_ts":9},"id":1}
```

**Exemplo - RATE_EXCEEDED:**

Resposta:
```json
{"jsonrpc":"2.0","result":{"status":"RATE_EXCEEDED","accepted_clicks":2,"rejected_clicks":3,"global_score":102,"node_score":52,"lamport_ts":10,"milestone":false,"milestone_value":null},"id":1}
```

---

## Metodo: `sync_offline`

**Descricao:** Reconcilia cliques acumulados durante periodo offline. Aplica rate limiting proporcional ao tempo de ausencia (`50 clicks/s * offline_seconds`).

**Parametros:**

| Campo | Tipo | Obrigatorio | Descricao |
|-------|------|------------|-----------|
| `node_id` | integer | sim | Identificador do no |
| `accumulated_clicks` | integer | sim | Total de cliques acumulados enquanto offline |
| `lamport_ts` | integer | sim | Timestamp logico de Lamport |

**Retorno:**

Mesmo conjunto de campos que `add_clicks`, mais:

| Campo adicional | Tipo | Descricao |
|----------------|------|-----------|
| `local_score` | integer | Score local do no apos sync (alias de `node_score`) |
| `node_scores` | lista | Lista de `{node_id, local_score}` de todos os nos conhecidos |

**Erros possíveis:**

| Codigo | Condicao |
|--------|---------|
| -32000 | No nao encontrado no banco de dados |

**Exemplo:**

Requisicao (firmware envia com `id=5`):
```json
{"jsonrpc":"2.0","method":"sync_offline","params":{"node_id":0,"accumulated_clicks":150,"lamport_ts":42},"id":5}
```

Resposta:
```json
{"jsonrpc":"2.0","result":{"status":"SUCCESS","accepted_clicks":100,"rejected_clicks":50,"global_score":550,"local_score":200,"node_score":200,"lamport_ts":43,"milestone":false,"milestone_value":null,"node_scores":[{"node_id":0,"local_score":200},{"node_id":1,"local_score":180}]},"id":5}
```

---

## Metodo: `activate_powerup`

**Descricao:** Ativa o multiplicador de cliques (x3 por 10 segundos) para o no. Apenas um powerup pode estar ativo por vez por no.

**Parametros:**

| Campo | Tipo | Obrigatorio | Descricao |
|-------|------|------------|-----------|
| `node_id` | integer | sim | Identificador do no |

**Retorno (sucesso - powerup ativado):**

| Campo | Tipo | Descricao |
|-------|------|-----------|
| `status` | string | `"SUCCESS"` |
| `time_remaining` | integer | Duracao do powerup em segundos (sempre 10) |

**Retorno (powerup ja ativo):**

| Campo | Tipo | Descricao |
|-------|------|-----------|
| `error` | string | `"ALREADY_ACTIVE"` |
| `remaining` | float | Segundos restantes do powerup atual |

**Erros possíveis JSON-RPC:**

| Codigo | Condicao |
|--------|---------|
| -32000 | Excecao interna |

**Exemplo - ativacao:**

Requisicao:
```json
{"jsonrpc":"2.0","method":"activate_powerup","params":{"node_id":2},"id":3}
```

Resposta:
```json
{"jsonrpc":"2.0","result":{"status":"SUCCESS","time_remaining":10},"id":3}
```

**Exemplo - ja ativo:**

```json
{"jsonrpc":"2.0","result":{"error":"ALREADY_ACTIVE","remaining":6.4},"id":3}
```

---

## Metodo: `get_nodes_scores`

**Descricao:** Retorna os scores de todos os nos conhecidos, ordenados por score decrescente.

**Parametros:** Nenhum (objeto vazio `{}`).

**Retorno:**

Lista de objetos, cada um com:

| Campo | Tipo | Descricao |
|-------|------|-----------|
| `node_id` | integer | Identificador do no |
| `local_score` | integer | Score local acumulado |

Os dados sao lidos diretamente do banco SQLite (`nodes.local_score`).

**Erros possíveis:**

| Codigo | Condicao |
|--------|---------|
| -32000 | Falha de banco de dados |

**Exemplo:**

Requisicao (firmware envia com `id=4`):
```json
{"jsonrpc":"2.0","method":"get_nodes_scores","params":{},"id":4}
```

Resposta:
```json
{"jsonrpc":"2.0","result":[{"node_id":0,"local_score":310},{"node_id":1,"local_score":270},{"node_id":2,"local_score":130}],"id":4}
```

---

## Metodo: `get_active_nodes`

**Descricao:** Retorna nos com status `ACTIVE` ou `SYNCING` no momento da chamada.

**Parametros:** Nenhum.

**Retorno:**

Lista de objetos:

| Campo | Tipo | Descricao |
|-------|------|-----------|
| `node_id` | integer | Identificador do no |
| `ip` | string | Endereco IP do no |
| `status` | string | `"ACTIVE"` ou `"SYNCING"` |
| `last_seen` | float | Unix timestamp da ultima atividade |

**Exemplo:**

```json
{"jsonrpc":"2.0","method":"get_active_nodes","params":{},"id":20}
```
```json
{"jsonrpc":"2.0","result":[{"node_id":0,"ip":"192.168.0.11","status":"ACTIVE","last_seen":1740000042.5}],"id":20}
```

---

## Metodo: `set_processing_delay`

**Descricao:** Altera o delay artificial de processamento RPC em tempo real (ferramenta de demonstracao de concorrencia).

**Parametros:**

| Campo | Tipo | Obrigatorio | Descricao |
|-------|------|------------|-----------|
| `delay_ms` | integer | sim | Novo delay em milissegundos (0 desativa) |

**Retorno:**

| Campo | Tipo | Descricao |
|-------|------|-----------|
| `status` | string | `"SUCCESS"` |
| `new_delay` | integer | Valor do delay configurado |

**Exemplo:**

```json
{"jsonrpc":"2.0","method":"set_processing_delay","params":{"delay_ms":500},"id":99}
```
```json
{"jsonrpc":"2.0","result":{"status":"SUCCESS","new_delay":500},"id":99}
```

---

## Erros JSON-RPC padrao

| Codigo | Mensagem | Condicao |
|--------|---------|---------|
| -32700 | Parse error | JSON invalido (nao parseaval) |
| -32600 | Invalid Request | Objeto nao e dict ou `jsonrpc != "2.0"` |
| -32601 | Method not found | Metodo nao existe no `handlers` |
| -32000 | Server error: ... | Excecao nao tratada no handler |
