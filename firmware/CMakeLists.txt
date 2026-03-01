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

# Add executable. Default name is the project name, version 0.1

add_executable(firmware
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

if(STRESS_TEST)
    target_sources(firmware PRIVATE
        tests/stress_test.c
    )
    target_compile_definitions(firmware PRIVATE STRESS_TEST=1)
    message(STATUS "STRESS_TEST habilitado")
endif()

pico_set_program_name(firmware "firmware")
pico_set_program_version(firmware "0.1")

# Generate PIO headers
pico_generate_pio_header(firmware ${CMAKE_CURRENT_LIST_DIR}/blink.pio)
pico_generate_pio_header(firmware ${CMAKE_CURRENT_LIST_DIR}/drivers/display/ws2812.pio)

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(firmware 1)
pico_enable_stdio_usb(firmware 1)

# Add the standard library to the build
# (pico_multicore adicionado para suportar o Core 1)
target_link_libraries(firmware
        pico_stdlib
        pico_multicore
    pico_cyw43_arch_lwip_sys_freertos
    FreeRTOS-Kernel-Heap4
        hardware_i2c
        hardware_pio
        hardware_pwm
        hardware_adc
)

# Add the standard include files to the build
target_include_directories(firmware PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
        ${CMAKE_CURRENT_LIST_DIR}/audio
        ${CMAKE_CURRENT_LIST_DIR}/config
        ${CMAKE_CURRENT_LIST_DIR}/drivers/display
        ${CMAKE_CURRENT_LIST_DIR}/middleware
        ${CMAKE_CURRENT_LIST_DIR}/tasks
        ${CMAKE_CURRENT_LIST_DIR}/discovery
)


pico_add_extra_outputs(firmware)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    DISPLAY_SSD1306=1
    PICO_HEAP_SIZE=0x14000
    configTICK_RATE_HZ=1000
)

# Injeta credenciais de WiFi se fornecidas no build
if(DEFINED ENV{WIFI_SSID})
    set(WIFI_SSID "$ENV{WIFI_SSID}")
endif()

if(DEFINED ENV{WIFI_PASSWORD})
    set(WIFI_PASSWORD "$ENV{WIFI_PASSWORD}")
endif()

if(DEFINED WIFI_SSID AND DEFINED WIFI_PASSWORD)
    target_compile_definitions(firmware PRIVATE
        WIFI_SSID="${WIFI_SSID}"
        WIFI_PASSWORD="${WIFI_PASSWORD}"
    )
    message(STATUS "Credenciais WiFi configuradas para a rede: ${WIFI_SSID}")
else()
    message(WARNING "Credenciais WIFI_SSID e WIFI_PASSWORD não foram fornecidas no build! A conexão WiFi falhará.")
endif()

# Configuração do NODE_ID
if(DEFINED ENV{NODE_ID})
    set(NODE_ID "$ENV{NODE_ID}")
elseif(NOT DEFINED NODE_ID)
    set(NODE_ID "0") # Fallback default
endif()

target_compile_definitions(firmware PRIVATE
    NODE_ID=${NODE_ID}
)
if(DEFINED FALLBACK_SERVER_IP)
    target_compile_definitions(firmware PRIVATE FALLBACK_SERVER_IP="${FALLBACK_SERVER_IP}")
    message(STATUS "Fallback IP configurado como: ${FALLBACK_SERVER_IP}")
endif()

message(STATUS "NODE_ID configurado como: ${NODE_ID}")