# Copyright (c) 2026 Synaptics Incorporated
# SPDX-License-Identifier: Apache-2.0
#
# Lab C3: MIPI camera capture -> grayscale downscale -> ST7789V3 TFT preview.

cmake_minimum_required(VERSION 3.20.0)

# Custom "zds,st7789v" devicetree binding lives under this app's own
# dts/bindings/ - DTS_ROOT must be extended BEFORE find_package(Zephyr...)
# runs (same requirement as ai_labs/03_sensor_anomaly).
list(APPEND DTS_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(camera_tft_preview)

target_sources(app PRIVATE
  src/main.c
  src/tft_preview.c
)

# Pull in SR100 video HAL headers used by the video_syna_sr100_mipi driver's
# devicetree binding. ZEPHYR_BASE points at <workspace>/zephyr, so its parent
# is the workspace root regardless of where this lab folder itself lives.
target_include_directories(app PRIVATE
  $ENV{ZEPHYR_BASE}/../zephyr_srsdk/modules/hal_synaptics/video/sr100/includes
)
