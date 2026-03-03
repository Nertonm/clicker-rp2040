#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 || $# -gt 2 ]]; then
  echo "Uso: $0 <horas> [serial_device]"
  echo "Exemplo: $0 12 /dev/ttyACM0"
  exit 1
fi

HOURS="$1"
SERIAL_DEVICE="${2:-/dev/ttyACM0}"

if ! [[ "$HOURS" =~ ^[0-9]+$ ]] || [[ "$HOURS" -le 0 ]]; then
  echo "Erro: <horas> deve ser inteiro positivo"
  exit 1
fi

if ! command -v minicom >/dev/null 2>&1; then
  echo "Erro: minicom não encontrado"
  exit 1
fi

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LOG_DIR="${ROOT_DIR}/logs"
mkdir -p "${LOG_DIR}"

STAMP="$(date +%Y%m%d_%H%M%S)"
LOG_FILE="${LOG_DIR}/uart_capture_${STAMP}.log"
DURATION_SECONDS=$((HOURS * 3600))

echo "[UART_CAPTURE] dispositivo: ${SERIAL_DEVICE}"
echo "[UART_CAPTURE] duração: ${HOURS}h (${DURATION_SECONDS}s)"
echo "[UART_CAPTURE] log: ${LOG_FILE}"
echo "[UART_CAPTURE] iniciando..."

timeout "${DURATION_SECONDS}" minicom -D "${SERIAL_DEVICE}" -b 115200 -C "${LOG_FILE}" || true

echo "[UART_CAPTURE] finalizado"
echo "[UART_CAPTURE] log salvo em: ${LOG_FILE}"
