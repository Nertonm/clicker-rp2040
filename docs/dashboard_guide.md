# Guia do Dashboard: Abacate Clicker

Este documento descreve como iniciar e acessar o dashboard de visualização do AbacateOS após a refatoração arquitetural.

## Como Iniciar

O dashboard é iniciado automaticamente junto com o servidor principal (`app/main.py`).

### Pré-requisitos
- Python 3.10+
- Dependências instaladas (aiosqlite, websockets)

### Execução
A partir da raiz do projeto:

```bash
cd server
export PYTHONPATH=$PYTHONPATH:.
python3 -m app.main
```

Se estiver usando o ambiente virtual integrado:
```bash
cd server
./venv/bin/python3 -m app.main
```

## Acesso Web

Uma vez iniciado, o dashboard estará disponível nos seguintes endereços:

- **Interface Web (HTTP)**: [http://localhost:8080](http://localhost:8080)
- **Fluxo de Dados (WebSocket)**: `ws://localhost:8081` (usado internamente pelo JavaScript)

## Estrutura do Dashboard

Após a refatoração, o dashboard foi decomposto para facilitar a manutenção:

- **Configurações**: Portas e hosts são definidos em `server/app/config.py`.
- **Backend do Dashboard**:
  - `server/dashboard/http.py`: Serve os arquivos HTML/CSS/JS.
  - `server/dashboard/websocket.py`: Gerencia as conexões de tempo real.
  - `server/dashboard/state_service.py`: Coleta dados de outras camadas (Domain/Infra) para o dashboard.
- **Frontend**:
  - `server/dashboard/templates/index.html`
  - `server/dashboard/static/css/style.css`
  - `server/dashboard/static/js/dashboard.js`

## Robustez
O dashboard opera como uma `asyncio.Task` independente. Se o servidor HTTP ou WebSocket falhar por qualquer motivo (ex: porta ocupada), o servidor **JSON-RPC principal continuará operando normalmente**, garantindo que as placas Pico W não percam conexão.
