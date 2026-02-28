# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

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
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico_w CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(firmware C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Add executable. Default name is the project name, version 0.1

add_executable(firmware
    main.c
    middleware/button_handler.c
    middleware/shared_state.c
    middleware/core1_display.c
    drivers/display/display.c
    drivers/display/ssd1306_i2c.c
    drivers/display/ws2812.c
    audio/buzzer.c
    discovery/service_disc.c
)

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
        hardware_i2c
        hardware_pio
        hardware_pwm
        hardware_adc
        pico_cyw43_arch_lwip_poll
)

# Add the standard include files to the build
target_include_directories(firmware PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
        ${CMAKE_CURRENT_LIST_DIR}/audio
        ${CMAKE_CURRENT_LIST_DIR}/drivers/display
        ${CMAKE_CURRENT_LIST_DIR}/middleware
        ${CMAKE_CURRENT_LIST_DIR}/discovery
)


pico_add_extra_outputs(firmware)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    DISPLAY_SSD1306=1   
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
message(STATUS "NODE_ID configurado como: ${NODE_ID}")