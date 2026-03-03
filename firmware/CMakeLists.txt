# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Evita kit/cache de host gcc quando o projeto exige toolchain ARM
if (CMAKE_C_COMPILER MATCHES "ccache/bin/gcc$" OR CMAKE_C_COMPILER MATCHES "/gcc$")
    unset(CMAKE_C_COMPILER CACHE)
endif()
if (CMAKE_CXX_COMPILER MATCHES "ccache/bin/g\\+\\+$" OR CMAKE_CXX_COMPILER MATCHES "/g\\+\\+$")
    unset(CMAKE_CXX_COMPILER CACHE)
endif()

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0-a4)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (NOT EXISTS ${picoVscode} AND DEFINED ENV{PICO_SDK_PATH})
    get_filename_component(_PICO_SDK_SDK_DIR "$ENV{PICO_SDK_PATH}" DIRECTORY)
    get_filename_component(_PICO_SDK_ROOT_DIR "${_PICO_SDK_SDK_DIR}" DIRECTORY)
    set(picoVscode ${_PICO_SDK_ROOT_DIR}/cmake/pico-vscode.cmake)
endif()
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()

# Fallback do toolchain ARM quando pico-vscode.cmake não configura compilador
if (DEFINED ENV{PICO_SDK_PATH})
    get_filename_component(_PICO_SDK_SDK_DIR "$ENV{PICO_SDK_PATH}" DIRECTORY)
    get_filename_component(_PICO_SDK_ROOT_DIR "${_PICO_SDK_SDK_DIR}" DIRECTORY)
    set(_PICO_ARM_GCC ${_PICO_SDK_ROOT_DIR}/toolchain/${toolchainVersion}/bin/arm-none-eabi-gcc)
    set(_PICO_ARM_GXX ${_PICO_SDK_ROOT_DIR}/toolchain/${toolchainVersion}/bin/arm-none-eabi-g++)
    if (EXISTS ${_PICO_ARM_GCC} AND EXISTS ${_PICO_ARM_GXX})
        set(CMAKE_C_COMPILER ${_PICO_ARM_GCC} CACHE FILEPATH "ARM GCC compiler" FORCE)
        set(CMAKE_CXX_COMPILER ${_PICO_ARM_GXX} CACHE FILEPATH "ARM G++ compiler" FORCE)
    endif()
endif()
# ====================================================================================
set(PICO_BOARD pico_w CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(firmware C CXX ASM)

option(STRESS_TEST "Enable firmware stress test tasks" OFF)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Importa FreeRTOS Kernel (RP2040 port) após pico_sdk_init()
set(FREERTOS_KERNEL_PATH "${CMAKE_CURRENT_LIST_DIR}/../third_party/FreeRTOS-Kernel" CACHE PATH "Path to FreeRTOS Kernel" FORCE)
if (EXISTS "${FREERTOS_KERNEL_PATH}/portable/ThirdParty/GCC/RP2040/FreeRTOS_Kernel_import.cmake")
    include(${FREERTOS_KERNEL_PATH}/portable/ThirdParty/GCC/RP2040/FreeRTOS_Kernel_import.cmake)
else()
    message(FATAL_ERROR "FreeRTOS_Kernel_import.cmake não encontrado em ${FREERTOS_KERNEL_PATH}. Configure FREERTOS_KERNEL_PATH.")
endif()

# ---------------------------------------------------------------------------
# Injeta credenciais de WiFi e FALLBACK_SERVER_IP se fornecidas no ambiente.
# As variáveis CMake resultantes são usadas dentro de add_node_target().
# ---------------------------------------------------------------------------
if(DEFINED ENV{WIFI_SSID})
    set(WIFI_SSID "$ENV{WIFI_SSID}")
endif()
if(DEFINED ENV{WIFI_PASSWORD})
    set(WIFI_PASSWORD "$ENV{WIFI_PASSWORD}")
endif()
if(DEFINED ENV{FALLBACK_SERVER_IP})
    set(FALLBACK_SERVER_IP "$ENV{FALLBACK_SERVER_IP}")
endif()

if(DEFINED WIFI_SSID AND DEFINED WIFI_PASSWORD)
    message(STATUS "Credenciais WiFi configuradas para a rede: ${WIFI_SSID}")
else()
    message(WARNING "Credenciais WIFI_SSID e WIFI_PASSWORD não foram fornecidas no build! A conexão WiFi falhará.")
endif()

# ---------------------------------------------------------------------------
# Fontes comuns a todos os targets de nó.
# ---------------------------------------------------------------------------
set(FIRMWARE_SOURCES
    main.c
    freertos_hooks.c
    middleware/button_handler.c
    middleware/shared_state.c
    middleware/lamport.c
    middleware/app_queues.c
    middleware/game_events.c
    tasks/task_buttons.c
    tasks/task_rpc.c
    tasks/task_display.c
    tasks/task_monitor.c
    drivers/display/display.c
    drivers/display/ssd1306_i2c.c
    drivers/display/ws2812.c
    audio/buzzer.c
    discovery/service_disc.c
    rpc_client.c
)

# ---------------------------------------------------------------------------
# add_node_target(<NODE_NUM>)
#
# Cria um target executável "node<NODE_NUM>" com NODE_ID=<NODE_NUM> fixado
# em tempo de compilação. O binário gerado é nomeado avocado_node<NODE_NUM>.uf2.
# ---------------------------------------------------------------------------
function(add_node_target NODE_NUM)
    set(TARGET_NAME "node${NODE_NUM}")

    add_executable(${TARGET_NAME} ${FIRMWARE_SOURCES})

    # Stress test opcional (herdado da opção global)
    if(STRESS_TEST)
        target_sources(${TARGET_NAME} PRIVATE tests/stress_test.c)
        target_compile_definitions(${TARGET_NAME} PRIVATE STRESS_TEST=1)
        message(STATUS "${TARGET_NAME}: STRESS_TEST habilitado")
    endif()

    # Nome do programa gravado no cabeçalho UF2
    pico_set_program_name(${TARGET_NAME} "avocado_node${NODE_NUM}")
    pico_set_program_version(${TARGET_NAME} "0.1")

    # Cabeçalhos PIO: gerados apenas uma vez (por node0) para evitar que
    # o CMake registre múltiplos custom commands produzindo o mesmo arquivo
    # de saída (blink.pio.h, ws2812.pio.h) — o que causaria erro de configure.
    # node1 e node2 recebem ${CMAKE_CURRENT_BINARY_DIR} via include abaixo.
    if(NODE_NUM EQUAL 0)
        pico_generate_pio_header(${TARGET_NAME} ${CMAKE_CURRENT_LIST_DIR}/blink.pio)
        pico_generate_pio_header(${TARGET_NAME} ${CMAKE_CURRENT_LIST_DIR}/drivers/display/ws2812.pio)
    else()
        # Headers já serão gerados por node0; apenas adiciona o diretório de saída
        # e declara dependência para garantir ordem de compilação.
        target_include_directories(${TARGET_NAME} PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
        add_dependencies(${TARGET_NAME} node0)
    endif()

    # Saída serial via UART e USB
    pico_enable_stdio_uart(${TARGET_NAME} 1)
    pico_enable_stdio_usb(${TARGET_NAME} 1)

    # Bibliotecas do SDK e FreeRTOS
    target_link_libraries(${TARGET_NAME}
        pico_stdlib
        pico_multicore
        pico_cyw43_arch_lwip_sys_freertos
        FreeRTOS-Kernel-Heap4
        hardware_i2c
        hardware_pio
        hardware_pwm
        hardware_adc
    )

    # Diretórios de include
    target_include_directories(${TARGET_NAME} PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
        ${CMAKE_CURRENT_LIST_DIR}/audio
        ${CMAKE_CURRENT_LIST_DIR}/config
        ${CMAKE_CURRENT_LIST_DIR}/drivers/display
        ${CMAKE_CURRENT_LIST_DIR}/middleware
        ${CMAKE_CURRENT_LIST_DIR}/tasks
        ${CMAKE_CURRENT_LIST_DIR}/discovery
    )

    # Definições de compilação: flags globais + NODE_ID fixo
    target_compile_definitions(${TARGET_NAME} PRIVATE
        DISPLAY_SSD1306=1
        PICO_HEAP_SIZE=0x14000
        configTICK_RATE_HZ=1000
        NODE_ID=${NODE_NUM}
    )

    # Credenciais WiFi (injetadas via env/cmake -D)
    if(DEFINED WIFI_SSID AND DEFINED WIFI_PASSWORD)
        target_compile_definitions(${TARGET_NAME} PRIVATE
            WIFI_SSID="${WIFI_SSID}"
            WIFI_PASSWORD="${WIFI_PASSWORD}"
        )
    endif()

    # IP de fallback do servidor RPC (sobrescreve o padrão em rpc_client.c)
    if(DEFINED FALLBACK_SERVER_IP)
        target_compile_definitions(${TARGET_NAME} PRIVATE
            FALLBACK_SERVER_IP="${FALLBACK_SERVER_IP}"
        )
    endif()

    # Garante nome exato do arquivo de saída: avocado_node<N>.uf2
    # IMPORTANTE: deve vir ANTES de pico_add_extra_outputs — o SDK captura
    # OUTPUT_NAME no momento da chamada e não usa generator expressions.
    set_target_properties(${TARGET_NAME} PROPERTIES OUTPUT_NAME "avocado_node${NODE_NUM}")

    # Gera .uf2, .elf, .bin, .map, .hex (usa OUTPUT_NAME acima)
    pico_add_extra_outputs(${TARGET_NAME})

    message(STATUS "Target '${TARGET_NAME}' criado — NODE_ID=${NODE_NUM} → avocado_node${NODE_NUM}.uf2")
endfunction()

# ---------------------------------------------------------------------------
# Cria os três targets de nó
# ---------------------------------------------------------------------------
add_node_target(0)
add_node_target(1)
add_node_target(2)

# Target agregado: cmake --build . --target all_nodes compila os 3 binários
add_custom_target(all_nodes DEPENDS node0 node1 node2)

# ---------------------------------------------------------------------------
# Compatibilidade com a extensão VS Code do Pico SDK.
# 'firmware' é tratado como alias de node0 — sem código duplicado.
# ---------------------------------------------------------------------------
add_executable(firmware ALIAS node0)