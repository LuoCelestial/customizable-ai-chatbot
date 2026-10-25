cmake_minimum_required(VERSION 3.16)
project(assistant_ui_preview C CXX)
get_filename_component(PASSPORT_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(LV_BUILD_CONF_PATH "${CMAKE_CURRENT_LIST_DIR}/lv_conf.h" CACHE PATH "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
add_subdirectory("${PASSPORT_ROOT}/managed_components/lvgl__lvgl" lvgl)
add_executable(assistant_ui_preview preview.c
    "${PASSPORT_ROOT}/main/assistant_ui.c"
    "${PASSPORT_ROOT}/main/assistant_profile.c"
    "${PASSPORT_ROOT}/main/assistant_memory.c"
    "${PASSPORT_ROOT}/assets/fonts/passport_noto_sans_sc_16.c"
    "${PASSPORT_ROOT}/assets/fonts/passport_kaomoji_16.c"
    "${PASSPORT_ROOT}/components/bsp/src/bsp_display_rounding.c")
target_include_directories(assistant_ui_preview PRIVATE
    "${PASSPORT_ROOT}/main" "${PASSPORT_ROOT}/tests/audio_stubs"
    "${PASSPORT_ROOT}/components/bsp/include" "${PASSPORT_ROOT}/components/bsp/src")
target_link_libraries(assistant_ui_preview PRIVATE lvgl::lvgl m)
