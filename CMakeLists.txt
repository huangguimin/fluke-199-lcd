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
set(toolchainVersion RISCV_ZCB_RPI_2_1_1_3)
set(picotoolVersion 2.2.0)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================

set(PICO_BOARD pico2 CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(lcd_converter C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Add executable. Default name is the project name, version 0.1

if (TARGET tinyusb_device)
    add_executable(lcd_converter
            lcd_converter.c
            spi_lcd.c
            lcd_framebuffer.c
            lcd_st75320.c
            frame_stats.c
            sensor.c
            )

    # Add PIO source files
    pico_generate_pio_header(lcd_converter ${CMAKE_CURRENT_LIST_DIR}/lcd_capture.pio)
    pico_generate_pio_header(lcd_converter ${CMAKE_CURRENT_LIST_DIR}/duty_cycle.pio)

    # pull in common dependencies
    target_link_libraries(lcd_converter pico_stdlib)
    target_link_libraries(lcd_converter hardware_pio)
    target_link_libraries(lcd_converter hardware_dma)
    target_link_libraries(lcd_converter hardware_irq)
    target_link_libraries(lcd_converter hardware_spi)
    target_link_libraries(lcd_converter hardware_pwm)
    target_link_libraries(lcd_converter hardware_adc)

    # enable usb output, disable uart output
    pico_enable_stdio_usb(lcd_converter 1)
    pico_enable_stdio_uart(lcd_converter 0)

    # create map/bin/hex/uf2 file etc.
    pico_add_extra_outputs(lcd_converter)

    # add url via pico_set_program_url
elseif(PICO_ON_DEVICE)
    message("Skipping lcd_converter because TinyUSB submodule is not initialized in the SDK")
endif()