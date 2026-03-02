import os
from pathlib import Path

# Caminhos base
BASE_DIR = Path(__file__).parent.parent
DB_FILE = str(BASE_DIR / "avocado.db")

# Configurações RPC (TCP)
RPC_HOST = "0.0.0.0"
RPC_PORT = 8765

# Configurações Descoberta (UDP)
UDP_DISCOVER_PORT = 9999

# Configurações Dashboard
DASHBOARD_HTTP_PORT = 8080
DASHBOARD_WS_PORT = 8081  # WebSocket separado para facilitar roteamento manual

# Endpoint de diagnóstico HTTP
DEBUG_HTTP_PORT = int(os.environ.get("DEBUG_HTTP_PORT", 8090))
