#!/usr/bin/env bash
# build_all.sh — Compila os 3 binários do firmware (avocado_node0/1/2.uf2)
#
# Uso:
#   WIFI_SSID="MinhaRede" WIFI_PASSWORD="senha123" \
#   FALLBACK_SERVER_IP="192.168.0.10" ./build_all.sh
#
# Pré-requisitos:
#   - PICO_SDK_PATH apontando para o diretório do Pico SDK instalado
#   - cmake >= 3.13
#   - arm-none-eabi-gcc (instalado pelo pico-sdk ou sistema)

set -euo pipefail

# ---------------------------------------------------------------------------
# Validação de variáveis obrigatórias
# ---------------------------------------------------------------------------
missing=""
for var in WIFI_SSID WIFI_PASSWORD FALLBACK_SERVER_IP; do
    if [ -z "${!var:-}" ]; then
        missing="${missing} ${var}"
    fi
done
if [ -n "$missing" ]; then
    echo "[build_all] ERRO: Variáveis de ambiente obrigatórias ausentes:${missing}" >&2
    echo "[build_all] Uso: WIFI_SSID=... WIFI_PASSWORD=... FALLBACK_SERVER_IP=... $0" >&2
    exit 1
fi

if [ -z "${PICO_SDK_PATH:-}" ]; then
    echo "[build_all] ERRO: PICO_SDK_PATH não definido." >&2
    exit 1
fi

# ---------------------------------------------------------------------------
# Diretórios de trabalho (relativos ao script)
# ---------------------------------------------------------------------------
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"
DIST_DIR="${SCRIPT_DIR}/dist"

# Limpa builds anteriores
echo "[build_all] Limpando ${BUILD_DIR} e ${DIST_DIR}..."
rm -rf "${BUILD_DIR}" "${DIST_DIR}"
mkdir -p "${BUILD_DIR}" "${DIST_DIR}"

# ---------------------------------------------------------------------------
# Trap para limpar em caso de erro
# ---------------------------------------------------------------------------
trap 'echo "[build_all] Falha na linha $LINENO. Saindo." >&2' ERR

# ---------------------------------------------------------------------------
# CMake configure
# ---------------------------------------------------------------------------
echo "[build_all] Configurando CMake (PICO_SDK_PATH=${PICO_SDK_PATH})..."
cmake -S "${SCRIPT_DIR}" -B "${BUILD_DIR}" \
    -DPICO_BOARD=pico_w \
    -DWIFI_SSID="${WIFI_SSID}" \
    -DWIFI_PASSWORD="${WIFI_PASSWORD}" \
    -DFALLBACK_SERVER_IP="${FALLBACK_SERVER_IP}"

# ---------------------------------------------------------------------------
# Build dos 3 targets
# ---------------------------------------------------------------------------
echo "[build_all] Compilando all_nodes..."
cmake --build "${BUILD_DIR}" --target all_nodes \
    --parallel "$(nproc 2>/dev/null || sysctl -n hw.logicalcpu 2>/dev/null || echo 4)"

# ---------------------------------------------------------------------------
# Copia .uf2 para dist/
# ---------------------------------------------------------------------------
echo "[build_all] Copiando binários para ${DIST_DIR}..."
for n in 0 1 2; do
    src="${BUILD_DIR}/avocado_node${n}.uf2"
    dst="${DIST_DIR}/avocado_node${n}.uf2"
    if [ ! -f "${src}" ]; then
        echo "[build_all] ERRO: Arquivo esperado não encontrado: ${src}" >&2
        exit 1
    fi
    cp "${src}" "${dst}"
done

# ---------------------------------------------------------------------------
# Relatório final com SHA256
# ---------------------------------------------------------------------------
echo ""
echo "================================================================"
echo " Build concluído — binários em ${DIST_DIR}"
echo "================================================================"
for n in 0 1 2; do
    file="${DIST_DIR}/avocado_node${n}.uf2"
    sha=$(sha256sum "${file}" | awk '{print $1}')
    echo "  avocado_node${n}.uf2"
    echo "    Path:   ${file}"
    echo "    SHA256: ${sha}"
    echo ""
done
echo "================================================================"
echo " Grava cada .uf2 na placa correspondente em modo BOOTSEL."
echo "================================================================"
