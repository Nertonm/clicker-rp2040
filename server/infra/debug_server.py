"""
debug_server.py — Endpoint HTTP de diagnóstico em tempo real.

Fornece GET /debug retornando JSON com snapshot completo do estado do sistema:
  - Métricas de RPC (total de chamadas, ok, erros)
  - Estado do GameManager (scores, milestones, power-ups)
  - NodeRegistry (lista de nós e statuses)
  - Métricas de DB (writes, reads, queries lentas)
  - Violações de Lamport recentes

Uso:
    await start_debug_server(game_manager, node_registry, game_repo,
                             get_active_connections_fn, port=8090)

Exemplo de consulta:
    curl http://localhost:8090/debug | python3 -m json.tool
    curl http://localhost:8090/debug/metrics
    curl http://localhost:8090/debug/lamport_violations

Porta configurável via variável de ambiente:
    DEBUG_HTTP_PORT=8090  (padrão: 8090)
"""

import asyncio
import json
import time

from infra import db
from infra.logger import log_normal, log_error, get_metrics


async def _build_debug_snapshot(
    game_manager,
    node_registry,
    game_repo,
    get_active_connections_fn,
) -> dict:
    """Constrói o snapshot completo de diagnóstico."""
    metrics = get_metrics()

    # --- GameManager state ---
    gm_state = {
        "global_score": game_manager.global_score,
        "milestones_done": sorted(list(game_manager.milestones_done)),
        "nodes": {
            str(nid): {
                "score": nd["score"],
                "last_ts": nd["last_ts"],
                "multiplier": nd["multiplier"],
                "powerup_expire": nd["powerup_expire"],
                "powerup_active": nd["powerup_expire"] > time.time(),
            }
            for nid, nd in game_manager.nodes.items()
        },
    }

    # --- NodeRegistry state ---
    all_nodes = list(node_registry.nodes.items())
    nr_state = {
        str(nid): {
            "ip": nd["ip"],
            "status": nd["status"],
            "last_seen": nd["last_seen"],
            "idle_s": round(time.time() - nd["last_seen"], 1),
        }
        for nid, nd in all_nodes
    }

    # --- Violações recentes de Lamport ---
    try:
        violations = await db.get_recent_violations(limit=10)
    except Exception:
        violations = []

    # --- Eventos recentes ---
    try:
        recent_events = await db.get_recent_events(limit=10)
    except Exception:
        recent_events = []

    return {
        "ts": time.time(),
        "active_connections": get_active_connections_fn(),
        "metrics": metrics,
        "game_manager": gm_state,
        "node_registry": nr_state,
        "recent_lamport_violations": violations,
        "recent_events": recent_events,
    }


async def _http_handler(
    reader, writer,
    game_manager, node_registry, game_repo, get_active_connections_fn,
):
    """Handler HTTP mínimo para o endpoint de debug."""
    try:
        raw = await asyncio.wait_for(reader.readline(), timeout=5.0)
        request_line = raw.decode(errors="replace").strip()
        # Consumir o resto dos headers
        while True:
            line = await asyncio.wait_for(reader.readline(), timeout=2.0)
            if line in (b"\r\n", b"\n", b""):
                break

        method, path, *_ = (request_line + " ").split(" ", 3)

        if method != "GET":
            body = json.dumps({"error": "Method Not Allowed"})
            status = "405 Method Not Allowed"
        elif path.rstrip("/") in ("/debug", "/debug/full"):
            snapshot = await _build_debug_snapshot(
                game_manager, node_registry, game_repo, get_active_connections_fn
            )
            body = json.dumps(snapshot, indent=2, default=str)
            status = "200 OK"
            log_normal("[DEBUG]", "/debug solicitado")
        elif path.rstrip("/") == "/debug/metrics":
            body = json.dumps(get_metrics(), indent=2)
            status = "200 OK"
        elif path.rstrip("/") == "/debug/violations":
            violations = await db.get_recent_violations(limit=50)
            body = json.dumps(violations, indent=2, default=str)
            status = "200 OK"
        else:
            body = json.dumps({
                "endpoints": ["/debug", "/debug/metrics", "/debug/violations"]
            })
            status = "404 Not Found"

        response = (
            f"HTTP/1.1 {status}\r\n"
            f"Content-Type: application/json\r\n"
            f"Content-Length: {len(body.encode())}\r\n"
            f"Access-Control-Allow-Origin: *\r\n"
            f"\r\n"
            f"{body}"
        )
        writer.write(response.encode())
        await writer.drain()
    except asyncio.TimeoutError:
        pass
    except Exception as exc:
        log_error("[DEBUG]", "handler error", error=str(exc))
    finally:
        writer.close()
        try:
            await writer.wait_closed()
        except Exception:
            pass


async def start_debug_server(
    game_manager,
    node_registry,
    game_repo,
    get_active_connections_fn,
    host: str = "0.0.0.0",
    port: int = 8090,
):
    """Inicia o servidor HTTP de diagnóstico e retorna o objeto server."""
    import os
    port = int(os.environ.get("DEBUG_HTTP_PORT", port))

    server = await asyncio.start_server(
        lambda r, w: _http_handler(
            r, w,
            game_manager, node_registry, game_repo, get_active_connections_fn,
        ),
        host, port,
    )
    log_normal("[DEBUG]", "debug_server_started",
               host=host, port=port,
               endpoints=["/debug", "/debug/metrics", "/debug/violations"])
    return server
