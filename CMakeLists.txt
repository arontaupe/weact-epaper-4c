cmake_minimum_required(VERSION 3.13)

# Must be included before the project.
include(pico_sdk_import.cmake)

project(pico_epaper_4color C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(WIFI_SSID "" CACHE STRING "Wi-Fi network name")
set(WIFI_PASSWORD "" CACHE STRING "Wi-Fi network password")
set(WIFI_SSID_2 "" CACHE STRING "Second Wi-Fi network name")
set(WIFI_PASSWORD_2 "" CACHE STRING "Second Wi-Fi network password")
set(WIFI_SSID_3 "" CACHE STRING "Third Wi-Fi network name")
set(WIFI_PASSWORD_3 "" CACHE STRING "Third Wi-Fi network password")
set(WIFI_SSID_4 "" CACHE STRING "Fourth Wi-Fi network name")
set(WIFI_PASSWORD_4 "" CACHE STRING "Fourth Wi-Fi network password")
set(DISPLAY_TEXT "" CACHE STRING "Text to show on boot")
set(DISPLAY_BG 1 CACHE STRING "Boot display background color")
set(DISPLAY_ORIENTATION 0 CACHE STRING "Boot display orientation")

# Initialize the Pico SDK
pico_sdk_init()

# ------------------------------------------------------------------------------

# Build the E-Paper driver as a static library
add_library(epaper_display STATIC
    src/epaper_display.c
    src/epaper_display_2in9.c
    src/epaper_display_utils.c
    src/font.c
)

# Link required Pico SDK libraries to the static library
target_link_libraries(epaper_display PUBLIC
    pico_stdlib
    hardware_spi
)

# ------------------------------------------------------------------------------

# Build the example executable
add_executable(pico_epaper_4color_example
    examples/main.c
)

# Set include directories, so the example can access the library's headers.
target_include_directories(pico_epaper_4color_example PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/examples
)

# Link the example against the library
target_link_libraries(pico_epaper_4color_example
    epaper_display
    pico_cyw43_arch_lwip_threadsafe_background
)

target_compile_definitions(pico_epaper_4color_example PRIVATE
    WIFI_SSID="${WIFI_SSID}"
    WIFI_PASSWORD="${WIFI_PASSWORD}"
    WIFI_SSID_2="${WIFI_SSID_2}"
    WIFI_PASSWORD_2="${WIFI_PASSWORD_2}"
    WIFI_SSID_3="${WIFI_SSID_3}"
    WIFI_PASSWORD_3="${WIFI_PASSWORD_3}"
    WIFI_SSID_4="${WIFI_SSID_4}"
    WIFI_PASSWORD_4="${WIFI_PASSWORD_4}"
    DISPLAY_TEXT="${DISPLAY_TEXT}"
    DISPLAY_BG=${DISPLAY_BG}
    DISPLAY_ORIENTATION=${DISPLAY_ORIENTATION}
)

# Enable USB output, disable UART output
pico_enable_stdio_usb(pico_epaper_4color_example 1)
pico_enable_stdio_uart(pico_epaper_4color_example 0)

# Create map/bin/hex/uf2 files
pico_add_extra_outputs(pico_epaper_4color_example)
