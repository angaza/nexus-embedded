cmake_minimum_required(VERSION 3.13.1)

# Overlay to specify flash partitions and USART configuration
set(DTC_OVERLAY_FILE "${CMAKE_CURRENT_SOURCE_DIR}/stm32f103rb.overlay")

include($ENV{ZEPHYR_BASE}/cmake/app/boilerplate.cmake NO_POLICY_SCOPE)
project(Zephyr_Nucleo-F103RB)

FILE(GLOB app_sources ../src/*.c*)
target_sources(app PRIVATE ${app_sources})
