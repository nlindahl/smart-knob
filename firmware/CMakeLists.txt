cmake_minimum_required(VERSION 3.24)
# LVGL comes from Zephyr's own module (CONFIG_LVGL=y), configured in prj.conf.
set(ZEPHYR_MODULES "${CMAKE_CURRENT_LIST_DIR}/../../modules/hal/espressif"
  "${CMAKE_CURRENT_LIST_DIR}/../../modules/lib/gui/lvgl")
list(APPEND DTS_ROOT ${CMAKE_CURRENT_LIST_DIR})
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(smartknob)
target_include_directories(app PRIVATE ../core/include src)
target_sources(app PRIVATE src/main.c src/motor.c src/sensors.c src/storage.c
  ../core/src/project.c ../core/src/haptics.c ../core/src/leds.c
  ../core/src/protocol.c ../core/src/render.c ../core/src/encoder.c
  ../core/src/motor_control.c)
