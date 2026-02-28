import asyncio

class LamportClock():
    def __init__(self):
        self.nodes = {}
        self.local_lamport = 0
        self.lock = asyncio.Lock()

    # Registra nó e relógio lamport, que por padrão é 0.

    async def register(self, node_id):
        async with self.lock:
            if node_id not in self.nodes:
                self.nodes[node_id] = {
                    "last_lamport": 0
                }
            self.nodes[node_id]["last_lamport"] = 0

    # Atualiza o relógio lamport do nó.
    
    async def update(self, node_id, received_ts):
        async with self.lock:
            lamport = max(self.local_lamport, received_ts) + 1
            self.nodes[node_id]["last_lamport"] = lamport
            self.local_lamport = lamport
            return lamport

    # Retorna último relógio lamport do nó.
        
    async def get_last_by_node(self, node_id):
        async with self.lock:
            node = self.nodes.get(node_id)
            if node is None:
                return 0
            return node["last_lamport"]