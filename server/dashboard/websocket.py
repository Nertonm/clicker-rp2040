import asyncio
import json
import traceback
from websockets.asyncio.server import serve as ws_serve
from websockets.exceptions import ConnectionClosed

class DashboardWebSocketServer:
    def __init__(self, state_service, host="0.0.0.0", port=8081):
        self.state_service = state_service
        self.host = host
        self.port = port
        self.clients = set()

    async def start(self):
        """Inicia o servidor WebSocket."""
        return await ws_serve(self.ws_handler, self.host, self.port)

    async def ws_handler(self, websocket):
        """Gerencia conexões WebSocket individuais."""
        self.clients.add(websocket)
        try:
            # Envia estado inicial imediatamente
            state = await self.state_service.collect_state()
            await websocket.send(json.dumps(state))

            # Mantém conexão aberta
            async for _ in websocket:
                pass
        except ConnectionClosed:
            pass
        except Exception:
            traceback.print_exc()
        finally:
            self.clients.discard(websocket)

    async def broadcast(self, message: str):
        """Envia mensagem para todos os clientes conectados."""
        if not self.clients:
            return

        for ws in self.clients.copy():
            try:
                await ws.send(message)
            except ConnectionClosed:
                self.clients.discard(ws)
            except Exception:
                self.clients.discard(ws)

    async def broadcast_loop(self, interval=0.5):
        """Loop contínuo de atualização dos clientes."""
        while True:
            try:
                if self.clients:
                    state = await self.state_service.collect_state()
                    await self.broadcast(json.dumps(state))
            except Exception:
                traceback.print_exc()
            await asyncio.sleep(interval)
