import asyncio

DISCOVER_PREFIX = "COOKIE_DISCOVER"
SERVER_RESPONSE = "COOKIE_SERVER"
RPC_PORT = 8765

class DiscoveryProtocol(asyncio.DatagramProtocol):
    def __init__(self):
        self.transport = None

    def connection_made(self, transport):
        self.transport = transport

    # Recebe datagramas, checa validade, e retorna resposta.

    def datagram_received(self, data, addr):
        try:
            message = data.decode().strip()
            if message.starswith(DISCOVER_PREFIX):
                parts = message.split(":")
                if len(parts) == 3 and parts[1] == "NODE_ID":
                    response = f"{SERVER_RESPONSE}:{RPC_PORT}"
                    self.transport.sendto(response.encode(), addr)
        except Exception as e:
            response = {
                "error": "DISCOVERY_UDP_ERROR"
            }
            return response

    async def start_udp_discovery(loop):
        transport, protocol = await loop.create_datagram_endpoint(
            lambda: DiscoveryProtocol(),
            local_addr=("0.0.0.0", 9999),
            allow_broadcast=True
        )

        return transport, protocol