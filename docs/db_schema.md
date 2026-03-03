# Database Schema (SQLite)

Utiliza SQLite para persistência local, isolada no módulo `db.py`. O banco é armazenado no arquivo `avocado.db`.

## Tabelas

### `nodes`
Armazena o estado de presença e a pontuação autoritativa de cada placa Pico W.

| Campo | Tipo | Descrição |
| :--- | :--- | :--- |
| `node_id` | `TEXT` | ID único da placa (Chave Primária). |
| `ip` | `TEXT` | Último endereço IP conhecido. |
| `status` | `TEXT` | Estado atual (`ACTIVE`, `SYNCING`, `INACTIVE`). |
| `last_seen` | `REAL` | Timestamp (epoch) da última atividade. |
| `local_score` | `INT` | Pontuação total acumulada pelo nó. |

### `events` (Append-Only)
Log histórico de todos os lotes de cliques recebidos. **Nunca sofre UPDATE ou DELETE**.

| Campo | Tipo | Descrição |
| :--- | :--- | :--- |
| `id` | `INT` | ID sequencial automático. |
| `node_id` | `TEXT` | Referência ao nó que enviou os cliques. |
| `sent` | `INT` | Quantidade de cliques enviados no lote. |
| `accepted` | `INT` | Quantidade de cliques aceitos (pós-rate limit). |
| `rate_limited`| `INT` | Booleano (0/1) indicando se houve corte parcial. |
| `lamport_ts` | `INT` | Timestamp lógico Lamport da requisição. |
| `created_at` | `REAL` | Timestamp real do servidor no recebimento. |

### `milestones` (Append-Only)
Registro de quando marcos globais de pontuação foram atingidos.

| Campo | Tipo | Descrição |
| :--- | :--- | :--- |
| `id` | `INT` | ID sequencial automático. |
| `value` | `INT` | Valor do marco atingido (ex: 100, 500). |
| `node_id` | `TEXT` | Nó responsável pelo clique que cruzou o marco. |
| `lamport_ts` | `INT` | Timestamp lógico no momento da conquista. |
| `created_at` | `REAL` | Timestamp real da conquista. |

## Regras de Acesso
- O acesso ao banco deve ser feito **exclusivamente** via as funções assíncronas em `db.py`.
- Lógica de negócio (GameManager) e gestão de presença (NodeRegistry) não devem escrever SQL.
- A tabela `events` é estritamente para auditoria e histórico; modificações em registros existentes são proibidas.
