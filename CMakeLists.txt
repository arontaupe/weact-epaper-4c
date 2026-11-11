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
# Fallback access point, opened when none of the networks above can be joined.
set(AP_SSID "epaper-setup" CACHE STRING "Name of the fallback access point")
set(AP_PASSWORD "epaper1234" CACHE STRING "Password of the fallback access point (8 to 63 characters)")
set(DISPLAY_TEXT "" CACHE STRING "Text to show on boot")
set(DISPLAY_BG 1 CACHE STRING "Boot display background color")
set(DISPLAY_ORIENTATION 1 CACHE STRING "Boot display orientation (1 = landscape, 0 = portrait)")

# Initialize the Pico SDK
pico_sdk_init()

# ------------------------------------------------------------------------------

# Wi-Fi credentials and boot text go into a generated header, not into -D flags.
# Passing them as compile definitions breaks on ';' and '"' (build error) and
# silently changes a backslash sequence such as '\n' inside a password.
# Here every value becomes a C string literal, byte for byte.
if(NOT DISPLAY_BG MATCHES "^[0-3]$")
    message(FATAL_ERROR "DISPLAY_BG must be 0 (black), 1 (white), 2 (yellow) or 3 (red)")
endif()
if(NOT DISPLAY_ORIENTATION MATCHES "^[01]$")
    message(FATAL_ERROR "DISPLAY_ORIENTATION must be 0 (portrait) or 1 (landscape)")
endif()
string(LENGTH "${AP_PASSWORD}" ap_password_length)
if(ap_password_length LESS 8 OR ap_password_length GREATER 63)
    message(FATAL_ERROR "AP_PASSWORD must be 8 to 63 characters (WPA2)")
endif()
string(LENGTH "${AP_SSID}" ap_ssid_length)
if(ap_ssid_length LESS 1 OR ap_ssid_length GREATER 32)
    message(FATAL_ERROR "AP_SSID must be 1 to 32 characters")
endif()

function(epaper_c_escape out_var value)
    string(REPLACE "\\" "\\\\" escaped "${value}")
    string(REPLACE "\"" "\\\"" escaped "${escaped}")
    string(REPLACE "\r" "" escaped "${escaped}")
    string(REPLACE "\n" "\\n" escaped "${escaped}")
    set(${out_var} "${escaped}" PARENT_SCOPE)
endfunction()

foreach(name
        WIFI_SSID WIFI_PASSWORD
        WIFI_SSID_2 WIFI_PASSWORD_2
        WIFI_SSID_3 WIFI_PASSWORD_3
        WIFI_SSID_4 WIFI_PASSWORD_4
        AP_SSID AP_PASSWORD
        DISPLAY_TEXT)
    epaper_c_escape(${name}_C "${${name}}")
endforeach()

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/wifi_config.h.in
               ${CMAKE_BINARY_DIR}/generated/wifi_config.h
               @ONLY)

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

# Build the example executable. dhcpserver (from pico-examples, MIT) hands out
# addresses when the Pico runs its own fallback access point.
add_executable(pico_epaper_4color_example
    examples/main.c
    examples/dhcpserver/dhcpserver.c
)

# Set include directories, so the example can access the library's headers and
# the generated wifi_config.h.
target_include_directories(pico_epaper_4color_example PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/examples
    ${CMAKE_CURRENT_SOURCE_DIR}/examples/dhcpserver
    ${CMAKE_BINARY_DIR}/generated
)

# Link the example against the library
target_link_libraries(pico_epaper_4color_example
    epaper_display
    pico_cyw43_arch_lwip_threadsafe_background
)

# Enable USB output, disable UART output
pico_enable_stdio_usb(pico_epaper_4color_example 1)
pico_enable_stdio_uart(pico_epaper_4color_example 0)

# Create map/bin/hex/uf2 files
pico_add_extra_outputs(pico_epaper_4color_example)