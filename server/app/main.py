import asyncio
import socket
import traceback
from app import config
from infra import db
from infra.rpc_server import start_rpc_server
from infra.udp_discovery import start_udp_discovery
from infra.debug_server import start_debug_server
from domain.lamport_clock import LamportClock
from domain.node_registry import NodeRegistry
from domain.game_repository import GameRepository
from domain.game_manager import GameManager
from dashboard import start_dashboard

async def background_tasks(node_registry):
    """Roda tarefas de background como expiração de nós."""
    while True:
        try:
            await node_registry.mark_inactive()
        except Exception:
            traceback.print_exc()
        await asyncio.sleep(10)

def get_local_ip():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        # Não envia nada, apenas abre pra pegar a interface de saída
        s.connect(('8.8.8.8', 80))
        return s.getsockname()[0]
    except Exception:
        return "127.0.0.1"
    finally:
        s.close()

async def main():
    # 1. Inicializa Banco de Dados
    await db.init_db()
    
    # 2. Inicializa Domínio
    clock = LamportClock()
    registry = NodeRegistry(clock)
    game_repo = GameRepository()
    
    # Primitivas de sincronização para o GameManager
    game_lock = asyncio.Lock()
    manager = GameManager(registry, game_repo, clock, lock=game_lock)
    
    # 3. Inicia Infraestrutura: Servidor RPC (TCP)
    rpc_server, get_active_conns = await start_rpc_server(
        manager, registry, game_repo, 
        host=config.RPC_HOST, 
        port=config.RPC_PORT
    )
    
    # 4. Inicia Infraestrutura: Descoberta (UDP)
    loop = asyncio.get_running_loop()
    await start_udp_discovery(
        loop, 
        rpc_port=config.RPC_PORT, 
        discovery_port=config.UDP_DISCOVER_PORT
    )
    
    # 5. Inicia Dashboard (Task Independente)
    try:
        _broadcast_task, ws_server = await start_dashboard(
            game_manager=manager,
            node_registry=registry,
            get_active_connections_fn=get_active_conns,
            http_port=config.DASHBOARD_HTTP_PORT,
            ws_port=config.DASHBOARD_WS_PORT
        )
        # Injeta o notifier no GameManager após o dashboard estar pronto.
        # ws_server.broadcast tem a assinatura async(str) esperada pelo notifier.
        manager.notifier = ws_server.broadcast
    except Exception as e:
        print(f"[Dashboard] Falha ao iniciar: {e} - O servidor continuará sem dashboard.")

    # 6. Inicia Tasks de Background
    asyncio.create_task(background_tasks(registry))

    # 7. Inicia Servidor HTTP de Diagnóstico (/debug)
    debug_port = getattr(config, "DEBUG_HTTP_PORT", 8090)
    try:
        debug_server = await start_debug_server(
            game_manager=manager,
            node_registry=registry,
            game_repo=game_repo,
            get_active_connections_fn=get_active_conns,
            port=debug_port,
        )
        asyncio.create_task(debug_server.serve_forever())
    except Exception as e:
        print(f"[Debug] Falha ao iniciar debug server: {e} - continuando sem endpoint /debug.")
        debug_server = None

    local_ip = get_local_ip()
    debug_port_actual = getattr(config, "DEBUG_HTTP_PORT", 8090)
    print("\n" + "="*50)
    print(f"AbacateOS - SERVIDOR INICIADO")
    print(f"IP LOCAL: {local_ip}")
    print("="*50 + "\n")
    print(f"Serviços:")
    print(f" - RPC: {config.RPC_HOST}:{config.RPC_PORT}")
    print(f" - UDP Discovery: port {config.UDP_DISCOVER_PORT}")
    print(f" - Dashboard: http://{local_ip}:{config.DASHBOARD_HTTP_PORT}")
    print(f" - Debug:     http://{local_ip}:{debug_port_actual}/debug")
    
    async with rpc_server:
        await rpc_server.serve_forever()

if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        pass
