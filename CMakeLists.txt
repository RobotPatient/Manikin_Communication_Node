# SPDX-License-Identifier: Apache-2.0

#include <stm32h7xx_hal.h>
#include <stm32h7xx_hal_conf.h>
#include <stm32h7xx_hal_rcc.h>
#include <stm32h7xx_hal_pwr.h>

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(st_ble_sensor)
zephyr_include_directories(
    src
    src/ble
    src/message_processor
)



target_sources(app PRIVATE
  src/main.c
  src/basic_implementation.c
  src/can/can_transport.c
  src/message_processor/message_processor_simple.c
  src/ble/led_svc.c
  src/ble/ble_protocol.c
  src/ble/crc/crc16_koopman.c
  src/ble/crc/crc16_koopman_hw.c
  src/session/session.c
  src/sdcard/sdcard_module.c
  src/session/led_handler.c
  )

# Add Bluetooth sample includes for reference
zephyr_library_include_directories(${ZEPHYR_BASE}/samples/bluetooth)

