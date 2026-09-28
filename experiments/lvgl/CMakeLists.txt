cmake_minimum_required(VERSION 3.16)
project(pocket_lvgl_engine_spike C)

if(NOT DEFINED LVGL_SOURCE_DIR)
  message(FATAL_ERROR "LVGL_SOURCE_DIR is required")
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(LV_CONF_SKIP ON CACHE BOOL "" FORCE)
set(LV_BUILD_USE_KCONFIG OFF CACHE BOOL "" FORCE)
set(LV_BUILD_SET_CONFIG_OPTS OFF CACHE BOOL "" FORCE)
set(LV_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
set(LV_FETCH_DEPENDENCIES OFF CACHE BOOL "" FORCE)

add_subdirectory("${LVGL_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/lvgl" EXCLUDE_FROM_ALL)

# Physical display is owned by Pocket's fbdev adapter. LVGL remains a render engine only.
add_executable(pocket-lvgl-spike
  adapter.c
  workload.c
  spike.c
)
target_compile_options(pocket-lvgl-spike PRIVATE -Wall -Wextra -Werror -Wpedantic -O2)
target_include_directories(pocket-lvgl-spike PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(pocket-lvgl-spike PRIVATE lvgl m)
