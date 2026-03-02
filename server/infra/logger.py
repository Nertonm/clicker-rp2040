"""
logger.py — Logging estruturado centralizado para o backend Clicker RP2040.

Todos os subsistemas devem usar as funções deste módulo para garantir
formato consistente e rastreabilidade.

FORMATO DE SAÍDA:
    {"ts": "...", "tag": "[RPC]", "node": 0, "msg": "...", "k1": v1, ...}

TAGS DEFINIDAS:
    [INIT]   Boot e inicialização
    [RPC]    Dispatcher: entradas/saídas de chamadas RPC
    [GAME]   Lógica de jogo: cliques, scores, power-ups
    [SYNC]   Sincronização offline
    [LAMPORT] Relógio lógico
    [NODE]   NodeRegistry: registro, heartbeat, inatividade
    [DB]     Operações de banco de dados
    [DISC]   UDP Discovery
    [ERROR]  Erros e exceções
    [STATS]  Métricas agregadas periódicas
    [DEBUG]  Endpoint /debug

VERBOSIDADE:
    Controlada pela variável de ambiente LOG_LEVEL:
      ERROR   — apenas erros
      NORMAL  — transições críticas (padrão)
      VERBOSE — operações detalhadas
      TRACE   — tudo, incluindo payloads

    Exemplo:
      LOG_LEVEL=VERBOSE python -m server.app.main
"""

import os
import json
import time
import datetime
import threading

# ============================================================
# Níveis de verbosidade
# ============================================================
_LEVELS = {"ERROR": 0, "NORMAL": 1, "VERBOSE": 2, "TRACE": 3}
_LOG_LEVEL_NAME = os.environ.get("LOG_LEVEL", "NORMAL").upper()
_LOG_LEVEL = _LEVELS.get(_LOG_LEVEL_NAME, 1)

# ============================================================
# Métricas globais de diagnóstico (thread-safe com Lock simples)
# ============================================================
_metrics_lock = threading.Lock()
_metrics = {
    "rpc_calls_total": 0,
    "rpc_calls_ok": 0,
    "rpc_calls_error": 0,
    "lamport_violations": 0,
    "rate_limited_clicks": 0,
    "accepted_clicks": 0,
    "milestones_triggered": 0,
    "db_writes": 0,
    "db_reads": 0,
    "db_slow_queries": 0,         # queries > DB_SLOW_THRESHOLD_MS
    "nodes_registered": 0,
    "nodes_marked_inactive": 0,
}

DB_SLOW_THRESHOLD_MS = float(os.environ.get("DB_SLOW_MS", "50"))


def _now_iso() -> str:
    return datetime.datetime.now(datetime.timezone.utc).isoformat()


def _emit(tag: str, level: int, msg: str, **fields):
    """Serializa e imprime uma linha de log JSON."""
    entry = {"ts": _now_iso(), "tag": tag, "msg": msg}
    entry.update(fields)
    print(json.dumps(entry, default=str), flush=True)


# ============================================================
# API pública de logging por nível
# ============================================================

def log_error(tag: str, msg: str, **fields):
    """Sempre exibido (nível ERROR)."""
    if _LOG_LEVEL >= 0:
        _emit(tag, 0, msg, **fields)


def log_normal(tag: str, msg: str, **fields):
    """Exibido em nível NORMAL ou superior."""
    if _LOG_LEVEL >= 1:
        _emit(tag, 1, msg, **fields)


def log_verbose(tag: str, msg: str, **fields):
    """Exibido em nível VERBOSE ou superior."""
    if _LOG_LEVEL >= 2:
        _emit(tag, 2, msg, **fields)


def log_trace(tag: str, msg: str, **fields):
    """Exibido apenas em nível TRACE."""
    if _LOG_LEVEL >= 3:
        _emit(tag, 3, msg, **fields)


# ============================================================
# Rastreamento de transições de estado
# ============================================================

def log_state_transition(tag: str, ctx: str, from_state: str,
                          to_state: str, reason: str = "", **fields):
    """Loga uma transição de estado crítica."""
    log_normal(tag, f"TRANSIÇÃO {ctx}: {from_state} -> {to_state}",
               reason=reason, **fields)


# ============================================================
# API de métricas / contadores
# ============================================================

def metric_inc(key: str, amount: int = 1):
    """Incrementa um contador de diagnóstico de forma thread-safe."""
    with _metrics_lock:
        if key in _metrics:
            _metrics[key] += amount


def get_metrics() -> dict:
    """Retorna snapshot imutável das métricas atuais."""
    with _metrics_lock:
        return dict(_metrics)


def log_metrics(tag: str = "[STATS]"):
    """Imprime snapshot das métricas como log estruturado."""
    snap = get_metrics()
    log_normal(tag, "snapshot_métricas", **snap)


# ============================================================
# Context manager para medir tempo de operações críticas
# ============================================================

class TimedOp:
    """
    Context manager que mede duração de uma operação e loga resultado.

    Uso:
        async with TimedOp("[DB]", "insert_event", node=node_id) as op:
            await db.insert_event(...)
        # Loga automaticamente como VERBOSE se < DB_SLOW_THRESHOLD_MS
        # ou como ERROR se >= DB_SLOW_THRESHOLD_MS
    """
    def __init__(self, tag: str, op_name: str,
                 slow_threshold_ms: float = DB_SLOW_THRESHOLD_MS, **fields):
        self.tag = tag
        self.op_name = op_name
        self.slow_threshold_ms = slow_threshold_ms
        self.fields = fields
        self._start = None

    def __enter__(self):
        self._start = time.monotonic()
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        duration_ms = (time.monotonic() - self._start) * 1000.0
        if exc_type is not None:
            log_error(self.tag, f"{self.op_name} ERRO",
                      duration_ms=round(duration_ms, 2),
                      error=str(exc_val), **self.fields)
        elif duration_ms >= self.slow_threshold_ms:
            metric_inc("db_slow_queries")
            log_error(self.tag, f"{self.op_name} LENTO",
                      duration_ms=round(duration_ms, 2), **self.fields)
        else:
            log_verbose(self.tag, f"{self.op_name} ok",
                        duration_ms=round(duration_ms, 2), **self.fields)
        return False  # não supress exceções
