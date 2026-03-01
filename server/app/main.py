import asyncio
import traceback
from app import config
from infra import db
from infra.rpc_server import start_rpc_server
from infra.udp_discovery import start_udp_discovery
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
        await start_dashboard(
            game_manager=manager,
            node_registry=registry,
            get_active_connections_fn=get_active_conns,
            http_port=config.DASHBOARD_HTTP_PORT,
            ws_port=config.DASHBOARD_WS_PORT
        )
    except Exception as e:
        print(f"[Dashboard] Falha ao iniciar: {e} - O servidor continuará sem dashboard.")

    # 6. Inicia Tasks de Background
    asyncio.create_task(background_tasks(registry))

    print(f"AbacateOS iniciado!")
    print(f" - RPC: {config.RPC_HOST}:{config.RPC_PORT}")
    print(f" - UDP Discovery: port {config.UDP_DISCOVER_PORT}")
    print(f" - Dashboard: http://localhost:{config.DASHBOARD_HTTP_PORT}")
    
    async with rpc_server:
        await rpc_server.serve_forever()

if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        pass
