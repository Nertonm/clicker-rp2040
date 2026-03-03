import asyncio
from .state_service import DashboardStateService
from .websocket import DashboardWebSocketServer
from .http import DashboardHTTPServer

async def start_dashboard(game_manager, node_registry, get_active_connections_fn, http_port, ws_port):
    """
    Inicializa e inicia todos os componentes do dashboard.
    Retorna uma lista de tasks/servers criados.
    """
    state_service = DashboardStateService(game_manager, node_registry, get_active_connections_fn)
    
    http_server = DashboardHTTPServer(port=http_port)
    ws_server = DashboardWebSocketServer(state_service, port=ws_port)
    
    # Inicia servidores
    await http_server.start()
    await ws_server.start()

    # Injeta notifier tipado nos módulos de domínio via injeção de dependência.
    # ws_server.notify tem assinatura async(event_type: str, payload_json: str).
    game_manager.set_notifier(ws_server.notify)
    node_registry.set_notifier(ws_server.notify)

    # Inicia loop de broadcast
    broadcast_task = asyncio.create_task(ws_server.broadcast_loop())
    
    print(f"[Dashboard] Servidor HTTP em http://0.0.0.0:{http_port}")
    print(f"[Dashboard] Servidor WebSocket em ws://0.0.0.0:{ws_port}")
    
    return broadcast_task, ws_server
