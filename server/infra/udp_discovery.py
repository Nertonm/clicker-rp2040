import asyncio

DISCOVER_PREFIX = "COOKIE_DISCOVER"
SERVER_RESPONSE = "COOKIE_SERVER"

class DiscoveryDatagramProtocol(asyncio.DatagramProtocol):
    def __init__(self, rpc_port):
        self.transport = None
        self.rpc_port = rpc_port

    def connection_made(self, transport):
        self.transport = transport

    def datagram_received(self, data, addr):
        try:
            message = data.decode().strip()
            if message.startswith(DISCOVER_PREFIX):
                parts = message.split(":")
                if len(parts) == 3 and parts[1] == "NODE_ID":
                    response = f"{SERVER_RESPONSE}:{self.rpc_port}"
                    self.transport.sendto(response.encode(), addr)
        except Exception:
            pass

async def start_udp_discovery(loop, rpc_port, discovery_port=9999):
    transport, protocol = await loop.create_datagram_endpoint(
        lambda: DiscoveryDatagramProtocol(rpc_port),
        local_addr=("0.0.0.0", discovery_port),
        allow_broadcast=True
    )
    return transport, protocol
