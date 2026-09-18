cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(camera_usb_stream)

# Pulls in the Zephyr sample_usbd helper (sample_usbd.h/.c), reused verbatim
# from the SDK's mipi_capture_to_enc sample instead of hand-rolling USB
# device-stack bring-up.
include(${ZEPHYR_BASE}/samples/subsys/usb/common/common.cmake)

target_sources(app PRIVATE
	src/main.c
	src/usb_cdc_transport.c
)
