cmake_minimum_required(VERSION 3.24)
# LVGL 9.6 changed its Kconfig choices and source layout. Compile the pinned
# library directly with our adapter; Zephyr 4.4's 9.5 integration cannot load it.
set(SK_LVGL_DIR "${CMAKE_CURRENT_LIST_DIR}/../../modules/lib/gui/lvgl" CACHE PATH "LVGL source")
set(ZEPHYR_MODULES "${CMAKE_CURRENT_LIST_DIR}/../../modules/hal/espressif")
list(APPEND DTS_ROOT ${CMAKE_CURRENT_LIST_DIR})
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(smartknob)
file(GLOB_RECURSE LVGL_SOURCES CONFIGURE_DEPENDS "${SK_LVGL_DIR}/src/*.c")
zephyr_library_named(sk_lvgl)
zephyr_library_sources(${LVGL_SOURCES})
zephyr_library_include_directories(${SK_LVGL_DIR} ${SK_LVGL_DIR}/include)
zephyr_library_compile_definitions(LV_CONF_PATH="${CMAKE_CURRENT_SOURCE_DIR}/lv_conf.h")
target_link_libraries(app PUBLIC sk_lvgl)
target_include_directories(app PRIVATE ${SK_LVGL_DIR} ${SK_LVGL_DIR}/include)
target_compile_definitions(app PRIVATE LV_CONF_PATH="${CMAKE_CURRENT_SOURCE_DIR}/lv_conf.h")
target_include_directories(app PRIVATE ../core/include src)
target_sources(app PRIVATE src/main.c src/motor.c src/sensors.c src/storage.c
  ../core/src/project.c ../core/src/haptics.c ../core/src/leds.c
  ../core/src/protocol.c ../core/src/render.c ../core/src/encoder.c)
