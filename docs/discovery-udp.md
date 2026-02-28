# Localização Automática do Servidor por UDP Broadcast

Este documento descreve a implementação da User Story "Localização automática do servidor por UDP broadcast", que permite ao firmware descobrir o IP e a porta do servidor em tempo de execução, evitando hardcode de endpoint no binário de produção.

## Objetivo
Permitir que a placa, após conectar ao WiFi, descubra automaticamente o servidor do jogo via UDP broadcast, usando um pequeno protocolo de texto, com fallback controlado e feedback visual claro para o operador.

## Arquitetura e Módulos Envolvidos
- **Core 0 (Lógica + Rede):**
  - WiFi connect (cyw43 + lwIP).
  - Discovery UDP (`service_disc.c`).
  - Cliente RPC (`rpc_client.c`), que encapsula o endpoint.
  - Loop principal que envia cliques via RPC.
- **Core 1 (Apresentação):**
  - Atualiza OLED e matriz de LEDs.
  - Lê apenas o `shared_state` para saber:
    - `connection_status` (CONNECTING, ONLINE, OFFLINE).
    - `fallback_in_use` (se está usando IP de fallback).
- **Middleware:**
  - `shared_state.c`: expõe, além dos campos existentes, o flag `fallback_in_use`, indicando se o firmware está usando `DEV_SERVER_IP` em vez de um servidor descoberto.
- **Discovery:**
  - `discovery/service_disc.c` + `service_disc.h`:
    - Responsável por enviar broadcast UDP e esperar pela resposta.
    - Não conhece fallback nem estado global.
- **RPC:**
  - `rpc_client.c` + `rpc_client.h`:
    - Guarda o IP/porta do servidor em estado estático interno.
    - Exposto apenas via funções de alto nível (`set_server`, `set_server_fallback`, `send_clicks`).

## Protocolo de Discovery UDP

### Request (Placa → Rede)
Assim que o WiFi entra em estado `STATUS_ONLINE`, o Core 0 dispara um broadcast IPv4:
- Endereço de destino: `255.255.255.255`
- Porta de destino: `9999`
- Payload (texto ASCII):
```text
COOKIE_DISCOVER:NODE_ID:<n>
```
Onde `<n>` é o identificador do nó, passado em compile-time (`-DNODE_ID=0,1,2,...`) via CMake (`NODE_ID`).

### Response (Servidor → Placa)
Qualquer servidor compatível que receber esse pacote pode responder com:
```text
COOKIE_SERVER:<porta>
```
- `<porta>` é a porta TCP/HTTP onde o servidor RPC escuta.
- O IP do servidor é inferido a partir do IP de origem do pacote UDP de resposta.

O módulo `service_disc` valida a resposta checando o prefixo literal `COOKIE_SERVER:` e então:
- Converte `<porta>` em `uint16_t`.
- Copia o IP de origem (addr) do callback UDP para um `ip_addr_t`.

Se chegar mais de uma resposta, o firmware aceita a primeira válida que chegar dentro da janela de 5 segundos.

## Fluxo de Descoberta e Fallback

### 1. Discovery Inicial após WiFi OK
1. Core 0 conecta ao WiFi e marca `connection_status = STATUS_ONLINE`.
2. Imediatamente em seguida, executa o timeout discovery:
   - Cenário **sucesso**: IP oficial descoberto (estado encapsulado dentro de `rpc_client.c`) e OLED mostra `WIFI OK`.
   - Cenário **timeout**: `rpc_client_set_server_fallback()` é chamado para `DEV_SERVER_IP` + porta padrão, `fallback_in_use = true`, e OLED exibe `FALLBACK IP`.

### 2. Rediscovery após Queda e Reconexão de WiFi
- O firmware monitora a transição de `connection_status` no poll loop.
- Se status transita de != `STATUS_ONLINE` para `STATUS_ONLINE` (reconexão wi-fi), o Discovery se repete (aguardando 5s), substituindo o endpoint.

## Encapsulamento do Endpoint no Cliente RPC
O módulo `rpc_client.c` mantém o endpoint do servidor totalmente encapsulado via estado `static`, isolando a API e eliminando hardcodes em múltiplos pontos centrais.

## Invariantes e Decisões de Design
- **Separação de responsabilidades:** `service_disc` não dita fallback; `rpc_client` gerencia exclusividade de roteamento TCP; `main` orquestra o fluxo de eventos de rede; `core1_display` apenas reage a estado.
- **Resiliência:** Ausência de resposta ao UDP Broadcast não tranca a UX original; ocorre degradação graciosa avisando o painel OLED sobre o uso do ambiente de DEVFallback.
- **Portabilidade:** Pelo uso da raw api LWIP na porta 9999 com timeout, `NODE_ID` permite escalar o cluster do parque sem sobrepor as configurações num firmware final.
