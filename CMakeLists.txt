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
set(PICO_BOARD_HEADER_DIRS ${CMAKE_CURRENT_LIST_DIR}/boards)
set(PICO_BOARD pimoroni_tufty2350 CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(crisp-game-lib-portable C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Enable PIO support for the 5th bit of GPIO pins
add_compile_definitions(PICO_PIO_USE_GPIO_BASE=1)

file(GLOB SOURCES
	"src/*.cpp"
	"src/games/*.c"
	"src/lib/*.c"
)

file(GLOB HEADERS
	"src/*.h"
	"src/games/*.h"
	"src/lib/*.h"
)

add_executable(crisp-game-lib-portable ${SOURCES} ${HEADERS})

pico_set_program_name(crisp-game-lib-portable "crisp-game-lib-portable")
pico_set_program_version(crisp-game-lib-portable "1.2")

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(crisp-game-lib-portable 0)
pico_enable_stdio_usb(crisp-game-lib-portable 1)

add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/pimoroni-pico)

target_link_directories(
    crisp-game-lib-portable
    PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/pimoroni-pico
)

# Add the standard library to the build
target_link_libraries(
    crisp-game-lib-portable
    pico_stdlib
    pimoroni_bus
    pico_graphics
    hardware_powman
    hardware_adc
    hardware_i2c
    st7789
)

target_include_directories(
    crisp-game-lib-portable
    PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/pimoroni-pico
)

# Add the standard include files to the build
target_include_directories(crisp-game-lib-portable PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
)

pico_add_extra_outputs(crisp-game-lib-portable)