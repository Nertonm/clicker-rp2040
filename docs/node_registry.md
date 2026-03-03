# Node Registry: Gestão de Presença e Ciclo de Vida

O `NodeRegistry` é responsável por rastrear quais nós (Pico W) estão online, seus endereços IP e estados de sincronia. Ele opera de forma independente da lógica de jogo.

## Estados do Nó

| Estado | Descrição |
| :--- | :--- |
| `ACTIVE` | Nó online e processando cliques em tempo real. |
| `SYNCING` | Nó em processo de sincronização de cliques offline. |
| `INACTIVE` | Nó que não envia heartbeats há mais de 60 segundos. |

## Ciclo de Vida e Fluxo de Dados

1. **Registro Inicial (`register_node`)**:
   - Chamado quando o nó inicia ou reconecta.
   - Define o status como `ACTIVE` e registra o IP atual.
   - É uma operação idempotente (UPSERT).

2. **Manutenção de Presença (`heartbeat`)**:
   - Cada chamada RPC bem-sucedida ou endpoint de heartbeat dedicado atualiza o campo `last_seen`.
   - Se um nó `INACTIVE` enviar um heartbeat, ele é automaticamente reativado para `ACTIVE`.

3. **Detecção de Inatividade (`mark_inactive`)**:
   - Um job de background executa a cada 10 segundos.
   - Nós com `last_seen` superior a 60 segundos são marcados como `INACTIVE`.

4. **Sincronização (`update_status`)**:
   - O `GameManager` altera o status para `SYNCING` no início de um `sync_offline`.
   - Retorna para `ACTIVE` ao final do processo, garantindo que o nó apareça corretamente nas listagens de nós ativos.

## Monitoramento

O método `get_active_nodes` retorna uma lista de dicionários contendo o estado atual de todos os nós que não estão `INACTIVE`.

```python
[
    {
        "node_id": "pico_w_01",
        "ip": "192.168.0.15",
        "status": "ACTIVE",
        "last_seen": 1709145600.0
    },
    ...
]
```
