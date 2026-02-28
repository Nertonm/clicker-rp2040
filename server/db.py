import aiosqlite
import time
from pathlib import Path

DB_FILE = str(Path(__file__).with_name("avocado.db"))

async def init_db():
    """Inicializa o banco de dados com as tabelas nodes, events e milestones."""
    async with aiosqlite.connect(DB_FILE) as db:
        db.row_factory = aiosqlite.Row
        
        # Tabela de nós (Presença e Estado)
        await db.execute(
            """
                CREATE TABLE IF NOT EXISTS nodes(
                    node_id TEXT PRIMARY KEY,
                    ip TEXT NOT NULL,
                    status TEXT NOT NULL, 
                    last_seen REAL NOT NULL,
                    local_score INTEGER NOT NULL DEFAULT 0
                )
            """
        )

        # Tabela de eventos (Append-only)
        await db.execute (
            """
                CREATE TABLE IF NOT EXISTS events(
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    node_id TEXT NOT NULL,
                    sent INTEGER NOT NULL,
                    accepted INTEGER NOT NULL,
                    rate_limited INTEGER NOT NULL,
                    lamport_ts INTEGER NOT NULL,
                    created_at REAL NOT NULL
                )
            """
        )

        # Tabela de milestones (Append-only)
        await db.execute (
            """
                CREATE TABLE IF NOT EXISTS milestones(
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    value INTEGER NOT NULL,
                    node_id TEXT NOT NULL,
                    lamport_ts INTEGER NOT NULL,
                    created_at REAL NOT NULL
                )
            """
        )
        
        await db.commit()

async def insert_event(node_id, sent, accepted, rate_limited, lamport_ts):
    """Insere um novo evento de clique. A tabela events é estritamente append-only."""
    async with aiosqlite.connect(DB_FILE) as db:
        await db.execute(
            """
                INSERT INTO events (
                    node_id, sent, accepted, rate_limited, lamport_ts, created_at
                ) VALUES (?, ?, ?, ?, ?, ?)
            """, (node_id, sent, accepted, int(rate_limited), lamport_ts, time.time())
        )
        await db.commit()

async def get_recent_events(limit=50):
    """Retorna os eventos mais recentes."""
    async with aiosqlite.connect(DB_FILE) as db:
        db.row_factory = aiosqlite.Row
        async with db.execute(
            "SELECT * FROM events ORDER BY created_at DESC LIMIT ?", (limit,)
        ) as cursor:
            rows = await cursor.fetchall()
            return [dict(row) for row in rows]

async def insert_milestone(value, node_id, lamport_ts, created_at):
    """Insere uma conquista de milestone."""
    async with aiosqlite.connect(DB_FILE) as db:
        await db.execute(
            """
                INSERT INTO milestones (
                    value, node_id, lamport_ts, created_at
                ) VALUES (?, ?, ?, ?)
            """, (value, node_id, lamport_ts, created_at)
        )
        await db.commit()

async def update_node(node_id, ip, status, last_seen):
    """Upsert de informações do nó."""
    async with aiosqlite.connect(DB_FILE) as db:
        await db.execute(
            """
                INSERT INTO nodes (
                    node_id, ip, status, last_seen, local_score
                ) VALUES (?, ?, ?, ?, 0)
                ON CONFLICT (node_id) 
                DO UPDATE SET
                    ip=?,
                    status=?,
                    last_seen=?
            """, (node_id, ip, status, last_seen, ip, status, last_seen)
        )
        await db.commit()

async def update_node_status(node_id, status):
    """Atualiza apenas o status de um nó."""
    async with aiosqlite.connect(DB_FILE) as db:
        await db.execute(
            "UPDATE nodes SET status = ? WHERE node_id = ?",
            (status, node_id)
        )
        await db.commit()

async def get_node(node_id):
    """Busca um nó pelo ID."""
    async with aiosqlite.connect(DB_FILE) as db:
        db.row_factory = aiosqlite.Row
        async with db.execute(
            "SELECT * FROM nodes WHERE node_id = ?", (node_id,)
        ) as cursor:
            row = await cursor.fetchone()
            return dict(row) if row else None

async def get_all_nodes():
    """Retorna todos os nós cadastrados."""
    async with aiosqlite.connect(DB_FILE) as db:
        db.row_factory = aiosqlite.Row
        async with db.execute("SELECT * FROM nodes") as cursor:
            rows = await cursor.fetchall()
            return [dict(row) for row in rows]

async def update_node_score(node_id, local_score):
    """Atualiza o score de um nó específico."""
    async with aiosqlite.connect(DB_FILE) as db:
        await db.execute(
            "UPDATE nodes SET local_score = ? WHERE node_id = ?",
            (local_score, node_id)
        )
        await db.commit()

async def update_last_seen(node_id, last_seen):
    """Atualiza o timestamp de última atividade."""
    async with aiosqlite.connect(DB_FILE) as db:
        await db.execute(
            "UPDATE nodes SET last_seen = ? WHERE node_id = ?",
            (last_seen, node_id)
        )
        await db.commit()

async def get_nodes_scores():
    """Retorna o ranking de scores dos nós."""
    async with aiosqlite.connect(DB_FILE) as db:
        db.row_factory = aiosqlite.Row
        async with db.execute(
            "SELECT node_id, local_score FROM nodes ORDER BY local_score DESC"
        ) as cursor:
            rows = await cursor.fetchall()
            return [dict(row) for row in rows]

async def get_milestones_session(session_started_ts):
    """Retorna as milestones atingidas desde o início da sessão."""
    async with aiosqlite.connect(DB_FILE) as db:
        db.row_factory = aiosqlite.Row
        async with db.execute(
            """
                SELECT value, node_id, lamport_ts, created_at
                FROM milestones WHERE created_at >= ? ORDER BY lamport_ts ASC
            """, (session_started_ts,)
        ) as cursor:
            rows = await cursor.fetchall()
            return [dict(row) for row in rows]