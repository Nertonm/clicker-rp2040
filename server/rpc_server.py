import asyncio
import json
import time
import datetime
import traceback
from game_manager import GameManager
from node_registry import NodeRegistry
from game_repository import GameRepository
from lamport_clock import LamportClock
import db

# Configurações do servidor
RPC_HOST = "0.0.0.0"
RPC_PORT = 8765

# Parâmetro alterável em runtime - transformado em variável global
SIMULATE_PROCESSING_DELAY_MS = 0

# Estado global do servidor
active_connections = 0

def log_structured(node_id, method, processing_time_ms):
    """Gera uma linha de log estruturado em JSON conforme critérios de aceite."""
    log_entry = {
        "timestamp": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "node_id": node_id,
        "method": method,
        "processing_time_ms": round(processing_time_ms, 2),
        "active_connections": active_connections
    }
    print(json.dumps(log_entry), flush=True)

class RPCDispatcher:
    def __init__(self, game_manager, node_registry, game_repo):
        self.game_manager = game_manager
        self.node_registry = node_registry
        self.game_repo = game_repo
        self.handlers = {
            "get_active_nodes": self.node_registry.get_active_nodes,
            "add_clicks": self.game_manager.add_clicks,
            "sync_offline": self.game_manager.sync_offline,
            "activate_powerup": self.game_manager.activate_powerup,
            "register_node": self.node_registry.register_node,
            "heartbeat": self.node_registry.heartbeat,
            "get_nodes_scores": self.game_repo.get_nodes_scores,
            "set_processing_delay": self.set_processing_delay  # Adicionado conforme requisito de runtime
        }

    async def set_processing_delay(self, delay_ms):
        """Altera o delay de simulação em tempo real."""
        global SIMULATE_PROCESSING_DELAY_MS
        SIMULATE_PROCESSING_DELAY_MS = int(delay_ms)
        return {"status": "SUCCESS", "new_delay": SIMULATE_PROCESSING_DELAY_MS}

    async def dispatch(self, request_json):
        try:
            req = json.loads(request_json)
        except json.JSONDecodeError:
            return self._error_response(None, -32700, "Parse error")

        if not isinstance(req, dict) or req.get("jsonrpc") != "2.0":
            return self._error_response(req.get("id"), -32600, "Invalid Request")

        method = req.get("method")
        params = req.get("params", {})
        req_id = req.get("id")

        if method not in self.handlers or method.startswith("_"):
            return self._error_response(req_id, -32601, "Method not found")

        handler = self.handlers[method]
        
        start_process_time = time.time()
        node_id = params.get("node_id") if isinstance(params, dict) else None
        
        try:
            # Chama o handler.
            if isinstance(params, list):
                result = await handler(*params)
            elif isinstance(params, dict):
                result = await handler(**params)
            else:
                result = await handler()

            # Delay de simulação antes da serialização (requisito técnico)
            # Quando ativo, o log evidencia as múltiplas corrotinas concorrentes
            if SIMULATE_PROCESSING_DELAY_MS > 0:
                await asyncio.sleep(SIMULATE_PROCESSING_DELAY_MS / 1000)

            duration_ms = (time.time() - start_process_time) * 1000
            log_structured(node_id, method, duration_ms)

            return {
                "jsonrpc": "2.0",
                "result": result,
                "id": req_id
            }

        except Exception as e:
            traceback.print_exc()
            return self._error_response(req_id, -32000, f"Server error: {str(e)}")

    def _error_response(self, req_id, code, message):
        return {
            "jsonrpc": "2.0",
            "error": {"code": code, "message": message},
            "id": req_id
        }

async def handle_client(reader, writer, dispatcher):
    global active_connections
    active_connections += 1
    addr = writer.get_extra_info('peername')
    
    try:
        while True:
            # Uma requisição JSON-RPC por linha
            line = await reader.readline()
            if not line:
                break
            
            request_str = line.decode().strip()
            if not request_str:
                continue
                
            response = await dispatcher.dispatch(request_str)
            
            response_str = json.dumps(response) + "\n"
            writer.write(response_str.encode())
            await writer.drain()
            
    except Exception:
        pass
    finally:
        active_connections -= 1
        writer.close()
        try:
            await writer.wait_closed()
        except Exception:
            pass

async def background_tasks(node_registry):
    """Roda tarefas de background disparadas por start_server."""
    while True:
        try:
            await node_registry.mark_inactive()
        except Exception:
            traceback.print_exc()
        await asyncio.sleep(10)

async def main():
    await db.init_db()
    
    # Primitivas de sincronização criadas na infra e passadas para o domínio
    # para evitar que o domínio importe asyncio diretamente.
    game_lock = asyncio.Lock()
    
    clock = LamportClock()
    registry = NodeRegistry(clock)
    game_repo = GameRepository()
    manager = GameManager(registry, game_repo, clock, lock=game_lock) # Repos injetados
    dispatcher = RPCDispatcher(manager, registry, game_repo)

    # Inicia o servidor TCP assíncrono
    server = await asyncio.start_server(
        lambda r, w: handle_client(r, w, dispatcher),
        RPC_HOST, RPC_PORT
    )

    asyncio.create_task(background_tasks(registry))

    print(f"Servidor RPC rodando em {RPC_HOST}:{RPC_PORT} (JSON-RPC 2.0)")
    
    async with server:
        await server.serve_forever()

if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        pass
