# Contrato JSON‑RPC: Servidor para Firmware

Este documento define o formato de resposta JSON‑RPC 2.0 que o backend deve seguir para garantir a compatibilidade com o cliente de firmware (Pico W).

## 1. Visão Geral

O firmware utiliza um parser textual simplificado baseado em busca de substrings (`strstr`). **Não existe um parser JSON completo no firmware.** Por isso, o servidor deve seguir rigorosamente as convenções de nomes e formatos descritos aqui.

### Premissas do Cliente (Firmware)
- **Zero Heap**: O parsing é feito em buffers fixos na stack.
- **Busca por Chave**: O firmware localiza campos numéricos buscando a string `"\"campo\":"`.
- **Detecção Booleana**: Um campo é considerado `true` apenas se a string exata `"\"campo\":true"` for encontrada.
- **Tratamento de Erros**: A presença da chave `"error"` em qualquer lugar do payload invalida a resposta.

---

## 2. Estrutura da Resposta: `add_clicks`

Este método é o principal ponto de sincronização de estado.

### Envelope JSON‑RPC
```json
{
  "jsonrpc": "2.0",
  "result": {
    "status": "SUCCESS",
    "accepted_clicks": 10,
    "rejected_clicks": 0,
    "global_score": 1230,
    "node_score": 450,
    "lamport_ts": 89,
    "milestone": true,
    "milestone_value": 1000,
    "powerup_active": false,
    "powerup_remaining_s": 0
  },
  "id": 1
}
```

### Campos do Objeto `result`

| Campo | Tipo | Descrição | Regra de Parsing no Firmware |
| :--- | :--- | :--- | :--- |
| `status` | string | `"SUCCESS"` ou `"RATE_EXCEEDED"`. | Informativo (log). |
| `accepted_clicks` | int | Cliques efetivamente processados. | `strstr` + `strtoul`. |
| `rejected_clicks` | int | Cliques descartados por rate limit. | `strstr` + `strtoul`. |
| `global_score` | int | **Autoritativo.** Novo total global. | `strstr` + `strtoul`. |
| `node_score` | int | **Autoritativo.** Novo total do nó. | `strstr` + `strtoul`. |
| `lamport_ts` | int | Novo servidor de tempo lógico. | `strstr` + `strtoul`. |
| `milestone` | bool | Indica se um marco foi atingido. | Busca literal `"\"milestone\":true"`. |
| `milestone_value` | int/null | Valor do maior marco atingido. | `strstr` + `strtoul` (se != null). |
| `powerup_active` | bool | Indica se o power-up está ativo. | Busca literal `"\"powerup_active\":true"`. |
| `powerup_remaining_s`| int | Segundos restantes do power-up. | `strstr` + `strtoul`. |

---

## 3. Estrutura de Erro

Se o servidor encontrar um problema (método inexistente, violação de Lamport, etc.), deve retornar um objeto `error` padrão.

```json
{
  "jsonrpc": "2.0",
  "error": {
    "code": -32601,
    "message": "Method not found"
  },
  "id": 1
}
```

**Nota:** Para o firmware, a simples existência da substring `"\"error\""` dispara a lógica de rollback/restore de cliques. O firmware **não** parseia o código do erro para tomar decisões de negócio.

---

## 4. Exemplos de Payload

### Sucesso com Milestone e Power-up
```json
{
  "jsonrpc": "2.0",
  "result": {
    "status": "SUCCESS",
    "accepted_clicks": 10,
    "rejected_clicks": 0,
    "global_score": 1000,
    "node_score": 100,
    "lamport_ts": 55,
    "milestone": true,
    "milestone_value": 1000,
    "powerup_active": true,
    "powerup_remaining_s": 10
  },
  "id": 1
}
```

### Limite de Taxa Excedido (`RATE_EXCEEDED`)
O servidor aceita parte dos cliques e informa o descarte.
```json
{
  "jsonrpc": "2.0",
  "result": {
    "status": "RATE_EXCEEDED",
    "accepted_clicks": 5,
    "rejected_clicks": 15,
    "global_score": 1005,
    "node_score": 105,
    "lamport_ts": 56,
    "milestone": false,
    "milestone_value": null,
    "powerup_active": false,
    "powerup_remaining_s": 0
  },
  "id": 2
}
```

---

## 5. Regras de Compatibilidade e Evolução

Para manter o parsing do firmware estável:

1. **Nomes de Campos**: Devem ser exatamente como definidos (Case‑Sensitive).
2. **Booleanos**: Nunca use `0` ou `1`. Use sempre `true` ou `false` minúsculos, sem espaços extras entre a chave e o valor (ex: `"milestone":true`).
3. **Novos Campos**: O servidor pode adicionar novos campos no objeto `result`. O firmware irá ignorá-los automaticamente, desde que os nomes não colidam com os existentes.
4. **Campos Opcionais**: Se um campo numérico não for enviado, o firmware manterá o valor anterior em memória. No entanto, recomenda-se enviar todos os campos para garantir a sincronização autoritativa.
