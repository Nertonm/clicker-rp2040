import aiosqlite
import time
from app.config import DB_FILE
from infra.logger import (
    log_normal, log_verbose, log_error, log_trace,
    metric_inc, DB_SLOW_THRESHOLD_MS,
)


def _db_warn_slow(op: str, duration_ms: float, **fields):
    """Emite warning se operação DB exceder threshold de lentidão."""
    if duration_ms >= DB_SLOW_THRESHOLD_MS:
        metric_inc("db_slow_queries")
        log_error("[DB]", f"{op} LENTO",
                  duration_ms=round(duration_ms, 2), **fields)
    else:
        log_verbose("[DB]", f"{op} ok",
                    duration_ms=round(duration_ms, 2), **fields)


async def init_db():
    """Inicializa o banco de dados com as tabelas nodes, events e milestones."""
    t0 = time.monotonic()
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

        # Tabela de violações de Lamport (Append-only, auditoria)
        await db.execute(
            """
                CREATE TABLE IF NOT EXISTS lamport_violations(
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    node_id TEXT NOT NULL,
                    received_ts INTEGER NOT NULL,
                    server_ts INTEGER NOT NULL,
                    created_at REAL NOT NULL
                )
            """
        )
        
        await db.commit()
    dt = (time.monotonic() - t0) * 1000
    log_normal("[DB]", "init_db ok", db_file=str(DB_FILE), duration_ms=round(dt, 2))


async def insert_event(node_id, sent, accepted, rate_limited, lamport_ts):
    """Insere um novo evento de clique. A tabela events é estritamente append-only."""
    t0 = time.monotonic()
    try:
        async with aiosqlite.connect(DB_FILE) as db:
            await db.execute(
                """
                    INSERT INTO events (
                        node_id, sent, accepted, rate_limited, lamport_ts, created_at
                    ) VALUES (?, ?, ?, ?, ?, ?)
                """, (node_id, sent, accepted, int(rate_limited), lamport_ts, time.time())
            )
            await db.commit()
        metric_inc("db_writes")
        _db_warn_slow("insert_event",
                      (time.monotonic() - t0) * 1000,
                      node=node_id, sent=sent, accepted=accepted)
    except Exception as exc:
        log_error("[DB]", "insert_event ERRO",
                  node=node_id, error=str(exc))
        raise


async def get_recent_events(limit=50):
    """Retorna os eventos mais recentes."""
    t0 = time.monotonic()
    async with aiosqlite.connect(DB_FILE) as db:
        db.row_factory = aiosqlite.Row
        async with db.execute(
            "SELECT * FROM events ORDER BY created_at DESC LIMIT ?", (limit,)
        ) as cursor:
            rows = await cursor.fetchall()
    metric_inc("db_reads")
    log_trace("[DB]", "get_recent_events",
              rows=len(rows), duration_ms=round((time.monotonic()-t0)*1000, 2))
    return [dict(row) for row in rows]


async def insert_milestone(value, node_id, lamport_ts, created_at):
    """Insere uma conquista de milestone."""
    t0 = time.monotonic()
    try:
        async with aiosqlite.connect(DB_FILE) as db:
            await db.execute(
                """
                    INSERT INTO milestones (
                        value, node_id, lamport_ts, created_at
                    ) VALUES (?, ?, ?, ?)
                """, (value, node_id, lamport_ts, created_at)
            )
            await db.commit()
        metric_inc("db_writes")
        log_normal("[DB]", "insert_milestone",
                   value=value, node=node_id, lamport_ts=lamport_ts,
                   duration_ms=round((time.monotonic()-t0)*1000, 2))
    except Exception as exc:
        log_error("[DB]", "insert_milestone ERRO",
                  value=value, node=node_id, error=str(exc))
        raise


async def update_node(node_id, ip, status, last_seen):
    """Upsert de informações do nó."""
    t0 = time.monotonic()
    try:
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
        metric_inc("db_writes")
        _db_warn_slow("update_node",
                      (time.monotonic() - t0) * 1000,
                      node=node_id, status=status)
    except Exception as exc:
        log_error("[DB]", "update_node ERRO",
                  node=node_id, status=status, error=str(exc))
        raise


async def update_node_status(node_id, status):
    """Atualiza apenas o status de um nó."""
    t0 = time.monotonic()
    async with aiosqlite.connect(DB_FILE) as db:
        await db.execute(
            "UPDATE nodes SET status = ? WHERE node_id = ?",
            (status, node_id)
        )
        await db.commit()
    metric_inc("db_writes")
    log_verbose("[DB]", "update_node_status",
                node=node_id, status=status,
                duration_ms=round((time.monotonic()-t0)*1000, 2))


async def get_node(node_id):
    """Busca um nó pelo ID."""
    async with aiosqlite.connect(DB_FILE) as db:
        db.row_factory = aiosqlite.Row
        async with db.execute(
            "SELECT * FROM nodes WHERE node_id = ?", (node_id,)
        ) as cursor:
            row = await cursor.fetchone()
            result = dict(row) if row else None
    metric_inc("db_reads")
    log_trace("[DB]", "get_node", node=node_id, found=(result is not None))
    return result


async def get_all_nodes():
    """Retorna todos os nós cadastrados."""
    async with aiosqlite.connect(DB_FILE) as db:
        db.row_factory = aiosqlite.Row
        async with db.execute("SELECT * FROM nodes") as cursor:
            rows = await cursor.fetchall()
    metric_inc("db_reads")
    log_trace("[DB]", "get_all_nodes", count=len(rows))
    return [dict(row) for row in rows]


async def update_node_score(node_id, local_score):
    """Atualiza o score de um nó específico."""
    t0 = time.monotonic()
    async with aiosqlite.connect(DB_FILE) as db:
        await db.execute(
            "UPDATE nodes SET local_score = ? WHERE node_id = ?",
            (local_score, node_id)
        )
        await db.commit()
    metric_inc("db_writes")
    _db_warn_slow("update_node_score",
                  (time.monotonic() - t0) * 1000,
                  node=node_id, score=local_score)


async def update_last_seen(node_id, last_seen):
    """Atualiza o timestamp de última atividade."""
    t0 = time.monotonic()
    async with aiosqlite.connect(DB_FILE) as db:
        await db.execute(
            "UPDATE nodes SET last_seen = ? WHERE node_id = ?",
            (last_seen, node_id)
        )
        await db.commit()
    metric_inc("db_writes")
    log_trace("[DB]", "update_last_seen",
              node=node_id, duration_ms=round((time.monotonic()-t0)*1000, 2))


async def get_nodes_scores():
    """Retorna o ranking de scores dos nós."""
    async with aiosqlite.connect(DB_FILE) as db:
        db.row_factory = aiosqlite.Row
        async with db.execute(
            "SELECT node_id, local_score FROM nodes ORDER BY local_score DESC"
        ) as cursor:
            rows = await cursor.fetchall()
    metric_inc("db_reads")
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
    metric_inc("db_reads")
    return [dict(row) for row in rows]


async def insert_lamport_violation(node_id, received_ts, server_ts):
    """Registra uma violação de ordenação causal do relógio de Lamport."""
    t0 = time.monotonic()
    try:
        async with aiosqlite.connect(DB_FILE) as db:
            await db.execute(
                """
                    INSERT INTO lamport_violations (
                        node_id, received_ts, server_ts, created_at
                    ) VALUES (?, ?, ?, ?)
                """, (node_id, received_ts, server_ts, time.time())
            )
            await db.commit()
        metric_inc("db_writes")
        log_normal("[DB]", "insert_lamport_violation",
                   node=node_id, received_ts=received_ts, server_ts=server_ts,
                   duration_ms=round((time.monotonic()-t0)*1000, 2))
    except Exception as exc:
        log_error("[DB]", "insert_lamport_violation ERRO",
                  node=node_id, error=str(exc))
        raise


async def get_recent_violations(limit=50):
    """Retorna as violações mais recentes para auditoria."""
    async with aiosqlite.connect(DB_FILE) as db:
        db.row_factory = aiosqlite.Row
        async with db.execute(
            """
                SELECT * FROM lamport_violations 
                ORDER BY created_at DESC 
                LIMIT ?
            """, (limit,)
        ) as cursor:
            rows = await cursor.fetchall()
    metric_inc("db_reads")
    return [dict(row) for row in rows]