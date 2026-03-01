import asyncio
import json
import time
import datetime
import traceback

# Estado global do servidor
_active_connections = 0

def log_structured(node_id, method, processing_time_ms):
    """Gera uma linha de log estruturado em JSON conforme critérios de aceite."""
    log_entry = {
        "timestamp": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "node_id": node_id,
        "method": method,
        "processing_time_ms": round(processing_time_ms, 2),
        "active_connections": _active_connections
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
            "set_processing_delay": self.set_processing_delay
        }
        self.simulate_delay_ms = 0

    async def set_processing_delay(self, delay_ms):
        """Altera o delay de simulação em tempo real."""
        self.simulate_delay_ms = int(delay_ms)
        return {"status": "SUCCESS", "new_delay": self.simulate_delay_ms}

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
            if isinstance(params, list):
                result = await handler(*params)
            elif isinstance(params, dict):
                result = await handler(**params)
            else:
                result = await handler()

            if self.simulate_delay_ms > 0:
                await asyncio.sleep(self.simulate_delay_ms / 1000)

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
    global _active_connections
    _active_connections += 1
    try:
        while True:
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
        _active_connections -= 1
        writer.close()
        try:
            await writer.wait_closed()
        except Exception:
            pass

async def start_rpc_server(game_manager, node_registry, game_repo, host="0.0.0.0", port=8765):
    """Inicia o servidor TCP RPC e retorna o servidor e uma função para ler conexões ativas."""
    dispatcher = RPCDispatcher(game_manager, node_registry, game_repo)
    server = await asyncio.start_server(
        lambda r, w: handle_client(r, w, dispatcher),
        host, port
    )
    return server, lambda: _active_connections
