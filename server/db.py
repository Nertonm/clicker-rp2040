import aiosqlite
import time
from pathlib import Path

DB_FILE = str(Path(__file__).with_name("avocado.db"))

# Inicia o banco de dados com as tabelas nodes, events e milestones

async def init_db():
    async with aiosqlite.connect(DB_FILE) as db:
        await db.execute(
            """
                CREATE TABLE IF NOT EXISTS nodes(
                    node_id TEXT PRIMARY KEY,
                    ip TEXT,
                    status TEXT, 
                    last_seen REAL,
                    local_score INT
                )
            """
        )

        await db.execute (
            """
                CREATE TABLE IF NOT EXISTS events(
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    node_id TEXT,
                    clicks_sent INTEGER,
                    clicks_accepted INTEGER,
                    rate_limited INTEGER,
                    lamport_ts INTEGER,
                    created_at REAL
                )
            """
        )

        await db.execute (
            """
                CREATE TABLE IF NOT EXISTS milestones(
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    milestone_value INTEGER,
                    triggered_by TEXT,
                    lamport_ts INTEGER,
                    triggered_at REAL
                )
            """
        )
        
        await db.commit()

# Insere eventos em events.

async def insert_event(node_id, sent, accepted, rate_limited, lamport_ts):
    async with aiosqlite.connect(DB_FILE) as db:
        await db.execute(
            """
                INSERT INTO events (
                    node_id, clicks_sent, clicks_accepted, rate_limited, lamport_ts, created_at
                )VALUES (?, ?, ?, ?, ?, ?)
            """, (node_id, sent, accepted, int(rate_limited), lamport_ts, time.time())
        )
        
        await db.commit()

# Insere milestone em milestones

async def insert_milestone(value, node_id, lamport_ts, now):
    async with aiosqlite.connect(DB_FILE) as db:
        await db.execute(
            """
                INSERT INTO milestones (
                    milestone_value, triggered_by, lamport_ts, triggered_at
                ) VALUES (?, ?, ?, ?)
            """, (value, node_id, lamport_ts, now)
        )

        await db.commit()

# Atualiza status do nó, caso nó não exista, insere-o na tabela.

async def update_node(node_id, ip, status, last_seen):
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

# Retorna informações armazenadas no banco de dados de um nó especifíco.

async def get_node(node_id):
    async with aiosqlite.connect(DB_FILE) as db:
        cursor = await db.execute(
            """
                SELECT node_id, ip, status, last_seen, local_score
                FROM nodes WHERE node_id = ?
            """, (node_id,)
        )
        row = await cursor.fetchone()
        await cursor.close()
        if row is None: return None
        return {
                "node_id": row[0],
                "ip": row[1],
                "status": row[2],
                "last_seen": row[3],
                "local_score": row[4]
        }

# Retorna informações armazenadas no banco de dados de todos os nós.

async def get_all_nodes():
    async with aiosqlite.connect(DB_FILE) as db:
        cursor = await db.execute(
            """
                SELECT node_id, ip, status, last_seen FROM nodes
            """
        )
        rows = await cursor.fetchall()
        await cursor.close()
        return [
            {
                "node_id": row[0],
                "ip": row[1],
                "status": row[2],
                "last_seen": row[3]
            } for row in rows
        ]

# Atualiza score local do nó especifíco.

async def update_node_score(node_id, local_score):
    async with aiosqlite.connect(DB_FILE) as db:
        await db.execute(
            """
                UPDATE nodes SET local_score = ? WHERE node_id = ?
            """, (local_score, node_id)
        )
        await db.commit()

# Atualiza last_seen do nó, é chamada por NodeRegistry a cada heartbeat().

async def update_last_seen(node_id, last_seen):
    async with aiosqlite.connect(DB_FILE) as db:
        await db.execute(
            """
                UPDATE nodes SET last_seen = ? WHERE node_id = ?
            """, (last_seen, node_id)
        )
        await db.commit()

# Retorna os scores armazenados de todos os nós.

async def get_nodes_scores():
    async with aiosqlite.connect(DB_FILE) as db:
        cursor = await db.execute(
            """
                SELECT node_id, local_score FROM nodes
            """
        )
        rows = await cursor.fetchall()
        await cursor.close()
        return [
            {
                "node_id": row[0],
                "local_score": row[1]
            } for row in rows
        ]

# Retorna todas as milestones alcançadas em uma sessão.

async def get_milestones_session(session_started_ts):
    async with aiosqlite.connect(DB_FILE) as db:
        cursor = await db.execute(
            """
                SELECT milestone_value, triggered_by, lamport_ts, triggered_at
                FROM milestones WHERE triggered_at >= ? ORDER BY lamport_ts ASC
            """, (session_started_ts,)
        )
        rows = await cursor.fetchall()
        await cursor.close()
        return [
            {
                "milestone_value": row[0],
                "triggered_by": row[1],
                "lamport_ts": row[2],
                "triggered_at": row[3]
            } for row in rows
        ]