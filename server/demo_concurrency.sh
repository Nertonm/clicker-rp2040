#!/usr/bin/env bash
# demo_concurrency.sh — Demonstração de concorrência do servidor JSON-RPC
#
# Envia 3 requisições add_clicks simultâneas (uma por nó) para tornar
# visible a metrica active_connections=3 no log do servidor.
#
# Uso:
#   SERVER_IP=192.168.0.10 SERVER_PORT=8765 ./server/demo_concurrency.sh
#
# Dependências: python3 (sem pacotes extras)

set -euo pipefail

SERVER_IP="${SERVER_IP:-localhost}"
SERVER_PORT="${SERVER_PORT:-8765}"

echo "[demo] Conectando a ${SERVER_IP}:${SERVER_PORT}"
echo "[demo] Enviando 3 requisições simultâneas (node_id 0, 1, 2)..."
echo ""

# Cada processo Python abre uma conexão TCP independente, mantém-na aberta
# por 1 segundo para que a janela de active_connections=3 fique visível no
# log do servidor, e então fecha.
send_rpc() {
    local NODE_ID="$1"
    local LAMPORT="$((100 + NODE_ID * 7))"   # timestamps distintos

    python3 - <<PYEOF
import socket, json, time, sys

host  = "${SERVER_IP}"
port  = ${SERVER_PORT}
node  = ${NODE_ID}
lts   = ${LAMPORT}

req = json.dumps({
    "jsonrpc": "2.0",
    "method": "add_clicks",
    "params": {"node_id": node, "clicks": 5, "lamport_ts": lts},
    "id": node
}) + "\n"

try:
    with socket.create_connection((host, port), timeout=5) as s:
        s.sendall(req.encode())
        time.sleep(1)  # mantém conexão aberta — active_connections permanece elevado
        resp = b""
        s.settimeout(2)
        try:
            while True:
                chunk = s.recv(4096)
                if not chunk:
                    break
                resp += chunk
        except socket.timeout:
            pass
    result = json.loads(resp.decode().strip()) if resp.strip() else {"error": "sem resposta"}
    print(f"[node{node}] {result}")
except Exception as e:
    print(f"[node{node}] ERRO: {e}", file=sys.stderr)
PYEOF
}

# Dispara as 3 conexões em background
send_rpc 0 &
PID0=$!
send_rpc 1 &
PID1=$!
send_rpc 2 &
PID2=$!

# Aguarda todas
wait $PID0 $PID1 $PID2

echo ""
echo "================================================================"
echo " Observe no log do servidor as linhas com:"
echo "   active_connections=3  asyncio_tasks=N"
echo " Cada linha dispatch_ok confirma processamento concorrente."
echo "================================================================"
