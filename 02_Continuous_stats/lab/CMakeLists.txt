# Copyright (c) 2026 Synaptics Incorporated
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(camera_continuous_stats)

target_sources(app PRIVATE src/main.c)

# Pull in SR100 video HAL headers used by the video_syna_sr100_mipi driver's
# devicetree binding. ZEPHYR_BASE points at <workspace>/zephyr, so its parent
# is the workspace root regardless of where this lab folder itself lives.
target_include_directories(app PRIVATE
  $ENV{ZEPHYR_BASE}/../zephyr_srsdk/modules/hal_synaptics/video/sr100/includes
)
