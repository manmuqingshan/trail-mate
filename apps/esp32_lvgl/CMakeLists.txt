cmake_minimum_required(VERSION 3.20)

project(TrailMateEsp32LvglAppShell LANGUAGES C CXX)

set(TRAIL_MATE_REPO_ROOT
    "${CMAKE_CURRENT_LIST_DIR}/../.."
    CACHE PATH "Trail Mate repository root")

include("${TRAIL_MATE_REPO_ROOT}/cmake/TrailMateUxPacks.cmake")
trailmate_add_ui_lvgl_ux_packs(trailmate_ui_lvgl_ux_packs)
include(CTest)

add_library(trailmate_esp32_lvgl_app_shell
    src/esp32_lvgl_idf_app_runtime_access.cpp
    src/esp32_lvgl_app_shell.cpp
    src/esp32_lvgl_startup_runtime.cpp
    src/esp32_lvgl_loop_runtime.cpp
    src/esp32_lvgl_runtime_config.cpp
    "${TRAIL_MATE_REPO_ROOT}/modules/product_composition/src/target_profile.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/product_composition/src/target_build_binding.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/product_composition/src/target_ux_binding.cpp")

target_include_directories(trailmate_esp32_lvgl_app_shell
    PUBLIC
        "${CMAKE_CURRENT_LIST_DIR}/src"
        "${TRAIL_MATE_REPO_ROOT}/modules/product_composition/include")

target_link_libraries(trailmate_esp32_lvgl_app_shell
    PUBLIC
        trailmate_ui_lvgl_ux_packs)

target_compile_features(trailmate_esp32_lvgl_app_shell PUBLIC cxx_std_17)

if(BUILD_TESTING)
    add_executable(trailmate_esp32_lvgl_app_shell_smoke
        tests/esp32_lvgl_app_shell_smoke.cpp)
    target_link_libraries(trailmate_esp32_lvgl_app_shell_smoke
        PRIVATE trailmate_esp32_lvgl_app_shell)
    add_test(NAME trailmate_esp32_lvgl_app_shell_smoke
             COMMAND trailmate_esp32_lvgl_app_shell_smoke)

    add_executable(trailmate_esp32_lvgl_startup_runtime_smoke
        tests/esp32_lvgl_startup_runtime_smoke.cpp)
    target_link_libraries(trailmate_esp32_lvgl_startup_runtime_smoke
        PRIVATE trailmate_esp32_lvgl_app_shell)
    add_test(NAME trailmate_esp32_lvgl_startup_runtime_smoke
             COMMAND trailmate_esp32_lvgl_startup_runtime_smoke)

    add_executable(trailmate_esp32_lvgl_loop_runtime_smoke
        tests/esp32_lvgl_loop_runtime_smoke.cpp)
    target_link_libraries(trailmate_esp32_lvgl_loop_runtime_smoke
        PRIVATE trailmate_esp32_lvgl_app_shell)
    add_test(NAME trailmate_esp32_lvgl_loop_runtime_smoke
             COMMAND trailmate_esp32_lvgl_loop_runtime_smoke)

    add_executable(trailmate_esp32_lvgl_runtime_config_smoke
        tests/esp32_lvgl_runtime_config_smoke.cpp)
    target_link_libraries(trailmate_esp32_lvgl_runtime_config_smoke
        PRIVATE trailmate_esp32_lvgl_app_shell)
    add_test(NAME trailmate_esp32_lvgl_runtime_config_smoke
             COMMAND trailmate_esp32_lvgl_runtime_config_smoke)

    add_executable(trailmate_esp32_lvgl_sd_coredump_contract_smoke
        tests/esp32_lvgl_sd_coredump_contract_smoke.cpp)
    target_compile_features(trailmate_esp32_lvgl_sd_coredump_contract_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_esp32_lvgl_sd_coredump_contract_smoke
             COMMAND trailmate_esp32_lvgl_sd_coredump_contract_smoke
                     "${TRAIL_MATE_REPO_ROOT}")

    add_executable(trailmate_product_target_profile_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/product_composition/tests/test_target_profile.cpp")
    target_link_libraries(trailmate_product_target_profile_smoke
        PRIVATE trailmate_esp32_lvgl_app_shell)
    add_test(NAME trailmate_product_target_profile_smoke
             COMMAND trailmate_product_target_profile_smoke)

    add_executable(trailmate_c6_hostlink_frame_codec_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/core_hostlink/tests/test_c6_frame_codec.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_hostlink/src/c6_frame_codec.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_hostlink/src/c6_frame_codec_c.c")
    target_include_directories(trailmate_c6_hostlink_frame_codec_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/core_hostlink/include")
    target_compile_features(trailmate_c6_hostlink_frame_codec_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_c6_hostlink_frame_codec_smoke
             COMMAND trailmate_c6_hostlink_frame_codec_smoke)

    add_executable(trailmate_c6_services_functional_smoke
        "${TRAIL_MATE_REPO_ROOT}/firmware/c6_companion/tests/test_tm_services_functional.cpp"
        "${TRAIL_MATE_REPO_ROOT}/firmware/c6_companion/components/tm_diag/tm_diag.c"
        "${TRAIL_MATE_REPO_ROOT}/firmware/c6_companion/components/tm_services/tm_services.c")
    target_include_directories(trailmate_c6_services_functional_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/firmware/c6_companion/tests/stubs"
            "${TRAIL_MATE_REPO_ROOT}/firmware/c6_companion/components/tm_diag/include"
            "${TRAIL_MATE_REPO_ROOT}/firmware/c6_companion/components/tm_services/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_hostlink/include")
    target_compile_features(trailmate_c6_services_functional_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_c6_services_functional_smoke
             COMMAND trailmate_c6_services_functional_smoke)

    add_executable(trailmate_product_target_build_binding_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/product_composition/tests/test_target_build_binding.cpp")
    target_link_libraries(trailmate_product_target_build_binding_smoke
        PRIVATE trailmate_esp32_lvgl_app_shell)
    add_test(NAME trailmate_product_target_build_binding_smoke
             COMMAND trailmate_product_target_build_binding_smoke)

    add_executable(trailmate_product_target_ux_binding_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/product_composition/tests/test_target_ux_binding.cpp")
    target_link_libraries(trailmate_product_target_ux_binding_smoke
        PRIVATE trailmate_esp32_lvgl_app_shell)
    add_test(NAME trailmate_product_target_ux_binding_smoke
             COMMAND trailmate_product_target_ux_binding_smoke)

    add_executable(trailmate_ui_presentation_target_ui_profile_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_presentation/tests/test_target_ui_profile.cpp")
    target_link_libraries(trailmate_ui_presentation_target_ui_profile_smoke
        PRIVATE trailmate_ui_lvgl_ux_packs)
    add_test(NAME trailmate_ui_presentation_target_ui_profile_smoke
             COMMAND trailmate_ui_presentation_target_ui_profile_smoke)

    add_executable(trailmate_ui_presentation_page_manifest_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_presentation/tests/test_page_manifest.cpp")
    target_link_libraries(trailmate_ui_presentation_page_manifest_smoke
        PRIVATE trailmate_ui_lvgl_ux_packs)
    add_test(NAME trailmate_ui_presentation_page_manifest_smoke
             COMMAND trailmate_ui_presentation_page_manifest_smoke)

    add_executable(trailmate_ui_presentation_layout_profile_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_presentation/tests/test_layout_profile.cpp")
    target_link_libraries(trailmate_ui_presentation_layout_profile_smoke
        PRIVATE trailmate_ui_lvgl_ux_packs)
    add_test(NAME trailmate_ui_presentation_layout_profile_smoke
             COMMAND trailmate_ui_presentation_layout_profile_smoke)

    add_executable(trailmate_ui_lvgl_t_display_p4_touch_ux_pack_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_lvgl_ux_packs/tests/test_t_display_p4_touch_ux_pack.cpp")
    target_link_libraries(trailmate_ui_lvgl_t_display_p4_touch_ux_pack_smoke
        PRIVATE trailmate_ui_lvgl_ux_packs)
    add_test(NAME trailmate_ui_lvgl_t_display_p4_touch_ux_pack_smoke
             COMMAND trailmate_ui_lvgl_t_display_p4_touch_ux_pack_smoke)

    add_executable(trailmate_ui_shared_t_display_p4_menu_profile_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/tests/test_t_display_p4_menu_profile.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/menu/menu_profile.cpp")
    target_include_directories(trailmate_ui_shared_t_display_p4_menu_profile_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/tests/stubs")
    target_compile_definitions(trailmate_ui_shared_t_display_p4_menu_profile_smoke
        PRIVATE TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4=1)
    target_compile_features(trailmate_ui_shared_t_display_p4_menu_profile_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_ui_shared_t_display_p4_menu_profile_smoke
             COMMAND trailmate_ui_shared_t_display_p4_menu_profile_smoke)

    add_executable(trailmate_ui_shared_t_display_p4_page_profile_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/tests/test_t_display_p4_page_profile.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/page/page_profile.cpp")
    target_include_directories(trailmate_ui_shared_t_display_p4_page_profile_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/tests/stubs")
    target_compile_definitions(trailmate_ui_shared_t_display_p4_page_profile_smoke
        PRIVATE TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4=1)
    target_compile_features(trailmate_ui_shared_t_display_p4_page_profile_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_ui_shared_t_display_p4_page_profile_smoke
             COMMAND trailmate_ui_shared_t_display_p4_page_profile_smoke)
endif()
