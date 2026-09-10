cmake_minimum_required(VERSION 3.20)
project(TrailMateTouchEditorTests LANGUAGES C CXX)
include(CTest)

set(LVGL_SOURCE_DIR "" CACHE PATH "LVGL 9.4 source directory")
if(NOT EXISTS "${LVGL_SOURCE_DIR}/lvgl.h")
    message(FATAL_ERROR "Set LVGL_SOURCE_DIR to the LVGL 9.4 dependency installed by PlatformIO")
endif()
get_filename_component(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
file(GLOB_RECURSE LVGL_SOURCES CONFIGURE_DEPENDS "${LVGL_SOURCE_DIR}/src/*.c")
add_library(touch_test_lvgl STATIC ${LVGL_SOURCES})
target_include_directories(touch_test_lvgl PUBLIC "${LVGL_SOURCE_DIR}")
if(UNIX)
    target_link_libraries(touch_test_lvgl PUBLIC m)
endif()
target_compile_definitions(touch_test_lvgl PUBLIC
    LV_CONF_SKIP=1 LV_USE_STDLIB_MALLOC=1 LV_USE_STDLIB_STRING=1
    LV_USE_STDLIB_SPRINTF=1 LV_FONT_MONTSERRAT_12=1 LV_FONT_MONTSERRAT_16=1)

add_executable(touch_text_editor_test
    test_touch_text_editor.cpp test_support.cpp
    "${REPO_ROOT}/modules/ui_lvgl_ux_packs/src/common/touch_text_editor.cpp"
    "${REPO_ROOT}/modules/ui_lvgl_ux_packs/src/common/input_layout.cpp"
    "${REPO_ROOT}/modules/ui_lvgl_ux_packs/src/packs/deck_touch_ux_pack.cpp"
    "${REPO_ROOT}/modules/ui_lvgl_ux_packs/src/ux/screen_registry.cpp"
    "${REPO_ROOT}/modules/ui_lvgl_ux_packs/src/ux/input_binding_set.cpp"
    "${REPO_ROOT}/modules/ui_presentation/src/page/page_manifest.cpp"
    "${REPO_ROOT}/modules/ui_shared/src/ui/widgets/ime/ime_widget.cpp"
    "${REPO_ROOT}/modules/ui_shared/src/ui/widgets/ime/pinyin_ime.cpp"
    "${REPO_ROOT}/modules/ui_shared/src/ui/widgets/ime/ime_input_mode_descriptor.cpp"
    "${REPO_ROOT}/modules/ui_shared/src/ui/page/page_profile.cpp")
target_include_directories(touch_text_editor_test PRIVATE
    "${REPO_ROOT}/modules/ui_lvgl_ux_packs/include"
    "${REPO_ROOT}/modules/ui_presentation/include"
    "${CMAKE_CURRENT_LIST_DIR}/stubs" "${REPO_ROOT}/modules/ui_shared/include")
target_compile_definitions(touch_text_editor_test PRIVATE
    UI_SHARED_TOUCH_IME_ENABLED=1)
target_compile_features(touch_text_editor_test PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(touch_text_editor_test PRIVATE /utf-8)
endif()
target_link_libraries(touch_text_editor_test PRIVATE touch_test_lvgl)
add_test(NAME touch_text_editor_test COMMAND touch_text_editor_test)
