#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FW_DIR="${ROOT_DIR}/firmware"
BUILD_DIR="${FW_DIR}/build"

ok() { echo "✅ $1"; }
fail() { echo "❌ $1"; exit 1; }
section() { echo; echo "=== $1 ==="; }

section "[1/5] Compilação Normal"
cmake -S "${FW_DIR}" -B "${BUILD_DIR}" >/dev/null
cmake --build "${BUILD_DIR}" >/dev/null
ok "Compilação normal OK"

section "[2/5] Compilação com STRESS_TEST"
cmake -S "${FW_DIR}" -B "${BUILD_DIR}" -DSTRESS_TEST=ON >/dev/null
cmake --build "${BUILD_DIR}" >/dev/null
ok "Compilação STRESS_TEST OK"

cmake -S "${FW_DIR}" -B "${BUILD_DIR}" -DSTRESS_TEST=OFF >/dev/null

section "[3/5] Encapsulamento lwIP"
LWIP_HITS="$(grep -RInE '^\s*#\s*include\s*[<\"]lwip/' "${FW_DIR}" \
  --include='*.c' --include='*.h' --exclude-dir='build' \
  --exclude='rpc_client.c' --exclude='service_disc.c' --exclude='service_disc.h' --exclude='lwipopts.h' || true)"
if [[ -n "${LWIP_HITS}" ]]; then
  echo "${LWIP_HITS}"
  fail "Encontrados includes/referências lwIP fora dos módulos permitidos"
fi
ok "Encapsulamento lwIP OK"

section "[4/5] Documentação"
[[ -f "${ROOT_DIR}/docs/architecture.md" ]] || fail "Falta docs/architecture.md"
[[ -f "${ROOT_DIR}/docs/shared-state.md" ]] || fail "Falta docs/shared-state.md"
[[ -f "${ROOT_DIR}/docs/atores-do-sistema.md" ]] || fail "Falta docs/atores-do-sistema.md"
[[ -f "${ROOT_DIR}/docs/matriz-leds.md" ]] || fail "Falta docs/matriz-leds.md"
[[ -f "${ROOT_DIR}/docs/rpc_client_api.md" ]] || fail "Falta docs/rpc_client_api.md"
ok "Documentação essencial presente"

section "[5/5] Estrutura de Tasks"
grep -q 'task_buttons' "${FW_DIR}/main.c" || fail "task_buttons não encontrada"
grep -q 'task_rpc' "${FW_DIR}/main.c" || fail "task_rpc não encontrada"
grep -q 'task_display' "${FW_DIR}/main.c" || fail "task_display não encontrada"
grep -q 'task_monitor' "${FW_DIR}/main.c" || fail "task_monitor não encontrada"
ok "Tasks esperadas presentes"

echo
echo "========================================"
echo "✅ TODAS AS VALIDAÇÕES PASSARAM"
echo "========================================"
echo "Próximos passos manuais:"
echo "1) Flashar firmware: picotool load firmware/build/firmware.uf2"
echo "2) Rodar modo stress: cmake -S firmware -B firmware/build -DSTRESS_TEST=ON && cmake --build firmware/build"
echo "3) Monitorar logs UART por janela longa"
