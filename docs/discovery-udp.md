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

## Robustez de RPC e Rediscovery Automático

Além do discovery inicial via UDP e do fallback para `DEV_SERVER_IP`, o firmware implementa um mecanismo de autoproteção contra servidores instáveis, baseado em contagem de falhas consecutivas no cliente RPC.

### Contador de falhas no rpc_client

O módulo `rpc_client.c` monitora internamente o sucesso/falha das chamadas de envio de cliques:

- **Estado interno:**
  - `static bool s_server_has_ip;` indica se há um endpoint configurado (descoberto ou fallback).
  - `static uint32_t s_rpc_fail_count;` conta falhas consecutivas de RPC.
  - `#define RPC_FAIL_THRESHOLD 5` define o limite de tolerância (atualmente 5).

- **Comportamento de `rpc_client_send_clicks(clicks)`:**
  - Se `s_server_has_ip == false`:
    - Loga um erro (sem servidor configurado) e retorna `false`.
  - Se há servidor configurado:
    - Tenta enviar os cliques (hoje com uma simulação de sucesso/falha; futuramente com envio real).
    - Em **sucesso**:
      - Zera `s_rpc_fail_count`.
      - Retorna `true`.
    - Em **falha**:
      - Incrementa `s_rpc_fail_count`.
      - Loga o número de falhas consecutivas.
      - Se `s_rpc_fail_count >= RPC_FAIL_THRESHOLD`:
        - Loga que o limite foi atingido.
        - Invalida o endpoint com `s_server_has_ip = false`.
        - Próximas chamadas a `rpc_client_send_clicks` falham imediatamente com "sem servidor configurado".

A API pública expõe apenas um getter simples:
- `bool rpc_client_has_server(void);`
  - Retorna o valor atual de `s_server_has_ip`.
  - Não mexe no contador nem tem efeitos colaterais.

### Rediscovery em caso de servidor instável

No loop principal do Core 0 (`main.c`), após consumir um batch de cliques:
- Se o WiFi está `STATUS_ONLINE`, o firmware tenta enviar o batch via `rpc_client_send_clicks(batch_clicks)` e guarda o resultado em `rpc_ok`.
- Em seguida, verifica se o cliente RPC ainda tem servidor:
  ```c
  if (current_status == STATUS_ONLINE && !rpc_client_has_server()) {
      // O rpc_client invalidou o endpoint após atingir o limiar de falhas
      shared_state_set_server_error_active(true);

      ip_addr_t disc_ip;
      uint16_t disc_port = 0;
      absolute_time_t deadline = make_timeout_time_ms(5000);
      bool found = service_disc_discover(&disc_ip, &disc_port, deadline);

      if (found) {
          rpc_client_set_server(&disc_ip, disc_port);
          shared_state_set_fallback_in_use(false);
      } else {
          rpc_client_set_server_fallback();
          shared_state_set_fallback_in_use(true);
      }

      shared_state_set_server_error_active(false);
  }
  ```
- Independente do motivo da falha (erro de RPC, endpoint inválido, stub local), o batch atual é sempre devolvido ao pool:
  ```c
  shared_state_restore_clicks(batch_clicks);
  ```
- Em sucesso, o batch é confirmado, o placar local é atualizado e os efeitos visuais (buzzer/LEDs) são disparados.

### Estado de erro de servidor para apresentação

Para tornar visível ao operador quando o sistema está em "modo recuperação", o `shared_state` inclui:
- `bool fallback_in_use;`
- `bool server_error_active;`

O Core 1 (`core1_display.c`) lê esses flags e ajusta OLED e LEDs:
- Em `STATUS_ONLINE`:
  - Se `server_error_active == true`:
    - OLED mostra "SERVER FAIL".
    - LED central da matriz (índice 12) acende vermelho forte (15, 0, 0), substituindo temporariamente o heartbeat branco.
  - Senão, se `fallback_in_use == true`:
    - OLED mostra "FALLBACK IP".
    - Heartbeat permanece branco fraco.
  - Caso contrário:
    - OLED mostra "WIFI OK".
    - Heartbeat branco normal.

Assim, o operador consegue distinguir:
- **WiFi OK + servidor OK** → "WIFI OK" + heartbeat branco.
- **WiFi OK + servidor usando fallback** → "FALLBACK IP" + heartbeat branco.
- **WiFi OK + servidor quebrado / rediscovering** → "SERVER FAIL" + LED central vermelho, durante a janela de rediscovery.

### Cenário de Teste: Falhas de RPC e Recuperação

Para validar a robustez:

1. Configure um cenário em que o envio RPC falhe repetidamente (na versão atual, o stub de `rpc_client_send_clicks` simula falha a cada N chamadas).
2. Gere cliques suficientes para que `s_rpc_fail_count` atinja o `RPC_FAIL_THRESHOLD`:
   - **Esperado nos logs:**
     - `[RPC] Falha RPC consecutiva #N` (até 5).
     - `[RPC] Limite de falhas atingido. Invalidando servidor atual.`
     - `[MAIN] Endpoint RPC invalidado por falhas. Rediscovery automático...`
     - Logs de discovery (`[DISC]`), seguidos de:
       - `[RPC] Endpoint oficial definido: X.X.X.X:YYYY` (se encontrou servidor), ou
       - `[RPC] Endpoint de FALLBACK definido: DEV_SERVER_IP:PORTA` (fallback).
3. Durante o rediscovery (cerca de 5s):
   - OLED deve exibir "SERVER FAIL".
   - LED central da matriz deve ficar vermelho forte.
4. Após o fim do rediscovery:
   - OLED retorna a "WIFI OK" ou "FALLBACK IP".
   - Heartbeat volta a ser branco.
   - Novos batches de cliques são enviados normalmente para o endpoint reconfigurado.

Esse mecanismo garante que a placa não fica presa indefinidamente a um servidor problemático: ela detecta falhas persistentes, invalida o endpoint, sinaliza o problema ao operador e tenta automaticamente se reconectar a um servidor saudável ou a um IP de fallback.
