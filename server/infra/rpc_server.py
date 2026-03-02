import asyncio
import json
import time
import traceback

from infra.logger import (
    log_normal, log_verbose, log_error, log_trace,
    metric_inc, get_metrics, log_metrics,
)

# Estado global do servidor
_active_connections = 0

# Contadores para dump periódico de stats (a cada N dispatches)
_dispatch_count = 0
_STATS_EVERY_N  = int(__import__("os").environ.get("RPC_STATS_EVERY", "100"))


class RPCDispatcher:
    def __init__(self, game_manager, node_registry, game_repo):
        self.game_manager = game_manager
        self.node_registry = node_registry
        self.game_repo = game_repo
        self.handlers = {
            "get_active_nodes":    self.node_registry.get_active_nodes,
            "add_clicks":          self.game_manager.add_clicks,
            "sync_offline":        self.game_manager.sync_offline,
            "activate_powerup":    self.game_manager.activate_powerup,
            "register_node":       self.node_registry.register_node,
            "heartbeat":           self.node_registry.heartbeat,
            "get_nodes_scores":    self.game_repo.get_nodes_scores,
            "set_processing_delay": self.set_processing_delay,
        }
        self.simulate_delay_ms = 0

    async def set_processing_delay(self, delay_ms):
        """Altera o delay de simulação em tempo real."""
        self.simulate_delay_ms = int(delay_ms)
        log_normal("[RPC]", "set_processing_delay", new_delay_ms=self.simulate_delay_ms)
        return {"status": "SUCCESS", "new_delay": self.simulate_delay_ms}

    async def dispatch(self, request_json: str, client_ip: str = None):
        global _dispatch_count

        # JSON parse
        try:
            req = json.loads(request_json)
        except json.JSONDecodeError as exc:
            log_error("[RPC]", "JSON parse error",
                      peer=client_ip, error=str(exc),
                      raw=request_json[:120])
            metric_inc("rpc_calls_error")
            return self._error_response(None, -32700, "Parse error")

        if not isinstance(req, dict) or req.get("jsonrpc") != "2.0":
            log_error("[RPC]", "Invalid JSON-RPC request",
                      peer=client_ip, req=str(req)[:80])
            metric_inc("rpc_calls_error")
            return self._error_response(req.get("id"), -32600, "Invalid Request")

        method  = req.get("method")
        params  = req.get("params", {})
        req_id  = req.get("id")
        node_id = params.get("node_id") if isinstance(params, dict) else None

        if method not in self.handlers or method.startswith("_"):
            log_error("[RPC]", "Method not found",
                      method=method, peer=client_ip)
            metric_inc("rpc_calls_error")
            return self._error_response(req_id, -32601, "Method not found")

        # Injeta IP para register_node
        if method == "register_node" and isinstance(params, dict):
            params["ip"] = client_ip

        handler    = self.handlers[method]
        t_start    = time.monotonic()
        metric_inc("rpc_calls_total")
        _dispatch_count += 1

        log_verbose("[RPC]", "dispatch_start",
                    method=method, node=node_id, peer=client_ip,
                    active_connections=_active_connections)

        try:
            if isinstance(params, list):
                res = handler(*params)
            elif isinstance(params, dict):
                res = handler(**params)
            else:
                res = handler()

            if asyncio.iscoroutine(res):
                result = await res
            else:
                result = res

            if self.simulate_delay_ms > 0:
                await asyncio.sleep(self.simulate_delay_ms / 1000)

            duration_ms = (time.monotonic() - t_start) * 1000.0
            metric_inc("rpc_calls_ok")

            log_normal("[RPC]", "dispatch_ok",
                       method=method, node=node_id,
                       duration_ms=round(duration_ms, 2),
                       active_connections=_active_connections)

            # Dump periódico de métricas
            if (_dispatch_count % _STATS_EVERY_N) == 0:
                log_metrics("[STATS]")

            return {"jsonrpc": "2.0", "result": result, "id": req_id}

        except Exception as exc:
            duration_ms = (time.monotonic() - t_start) * 1000.0
            metric_inc("rpc_calls_error")
            log_error("[RPC]", "dispatch_exception",
                      method=method, node=node_id,
                      duration_ms=round(duration_ms, 2),
                      error=str(exc))
            traceback.print_exc()
            return self._error_response(req_id, -32000, f"Server error: {exc}")

    def _error_response(self, req_id, code, message):
        return {
            "jsonrpc": "2.0",
            "error": {"code": code, "message": message},
            "id": req_id,
        }


async def handle_client(reader, writer, dispatcher):
    global _active_connections
    _active_connections += 1
    peer = writer.get_extra_info("peername")
    peer_str = f"{peer[0]}:{peer[1]}" if peer else "unknown"

    log_normal("[RPC]", "client_connected",
               peer=peer_str, active_connections=_active_connections)

    try:
        while True:
            line = await reader.readline()
            if not line:
                break
            request_str = line.decode().strip()
            if not request_str:
                continue

            log_trace("[RPC]", "recv_raw", peer=peer_str,
                      raw=request_str[:120])

            client_ip = peer[0] if peer else None
            response  = await dispatcher.dispatch(request_str, client_ip=client_ip)

            response_str = json.dumps(response) + "\n"
            writer.write(response_str.encode())
            await writer.drain()

    except Exception as exc:
        log_error("[RPC]", "client_handler_exception",
                  peer=peer_str, error=str(exc))
    finally:
        _active_connections -= 1
        log_normal("[RPC]", "client_disconnected",
                   peer=peer_str, active_connections=_active_connections)
        writer.close()
        try:
            await writer.wait_closed()
        except Exception:
            pass


async def start_rpc_server(game_manager, node_registry, game_repo,
                           host="0.0.0.0", port=8765):
    """Inicia o servidor TCP RPC e retorna o servidor e uma função para ler conexões ativas."""
    dispatcher = RPCDispatcher(game_manager, node_registry, game_repo)
    server = await asyncio.start_server(
        lambda r, w: handle_client(r, w, dispatcher),
        host, port,
    )
    log_normal("[RPC]", "server_started", host=host, port=port)
    return server, lambda: _active_connections

