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
  src/minimal_test.c
  src/can_wrapper_mock.c
  )

# Add Bluetooth sample includes for reference
zephyr_library_include_directories(${ZEPHYR_BASE}/samples/bluetooth)

