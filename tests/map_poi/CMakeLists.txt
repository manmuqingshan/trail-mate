cmake_minimum_required(VERSION 3.20)
project(map_poi_contract LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(REPO "${CMAKE_CURRENT_LIST_DIR}/../..")
set(CJSON_DIR "" CACHE PATH "cJSON 1.7.17 source and header directory")
add_executable(map_poi_contract test_poi.cpp
    ${CJSON_DIR}/cJSON.c
    ${REPO}/modules/ui_map_runtime/src/map_poi/poi_tile_source.cpp
    ${REPO}/modules/ui_map_runtime/src/map_poi/poi_snapshot_builder.cpp
    ${REPO}/modules/ui_map_runtime/src/map_tiles/map_tile_resolver.cpp
    ${REPO}/platform/esp/arduino_common/src/map_poi/cjson_poi_parser.cpp)
target_include_directories(map_poi_contract PRIVATE ${CJSON_DIR}
    ${REPO}/modules/ui_map_runtime/include ${REPO}/modules/ui_presentation/include
    ${REPO}/platform/esp/arduino_common/include)
enable_testing()
add_executable(map_poi_alignment_contract test_poi_alignment.cpp ${CJSON_DIR}/cJSON.c
    ${REPO}/modules/ui_map_runtime/src/map_poi/poi_tile_source.cpp
    ${REPO}/modules/ui_map_runtime/src/map_tiles/map_tile_resolver.cpp
    ${REPO}/platform/esp/arduino_common/src/map_poi/cjson_poi_parser.cpp)
target_include_directories(map_poi_alignment_contract PRIVATE ${CJSON_DIR}
    ${REPO}/modules/ui_map_runtime/include ${REPO}/modules/ui_presentation/include ${REPO}/platform/esp/arduino_common/include)
add_test(NAME map_poi_alignment_contract COMMAND map_poi_alignment_contract)
add_test(NAME map_poi_contract COMMAND map_poi_contract)
add_executable(map_annotation_parser_contract test_annotation_parser.cpp
    ${CJSON_DIR}/cJSON.c ${REPO}/platform/esp/arduino_common/src/map_poi/cjson_poi_parser.cpp)
target_include_directories(map_annotation_parser_contract PRIVATE ${CJSON_DIR}
    ${REPO}/modules/ui_map_runtime/include ${REPO}/modules/ui_presentation/include
    ${REPO}/platform/esp/arduino_common/include)
add_test(NAME map_annotation_parser_contract COMMAND map_annotation_parser_contract)
add_executable(validate_annotation_package validate_annotation_package.cpp
    ${CJSON_DIR}/cJSON.c ${REPO}/platform/esp/arduino_common/src/map_poi/cjson_poi_parser.cpp)
target_include_directories(validate_annotation_package PRIVATE ${CJSON_DIR}
    ${REPO}/modules/ui_map_runtime/include ${REPO}/modules/ui_presentation/include ${REPO}/platform/esp/arduino_common/include)
if(MSVC)
    target_compile_options(validate_annotation_package PRIVATE /utf-8 /EHsc)
endif()
add_executable(map_annotation_layout_contract test_annotation_layout.cpp
    ${REPO}/modules/ui_map_runtime/src/map_poi/annotation_layout.cpp)
target_include_directories(map_annotation_layout_contract PRIVATE
    ${REPO}/modules/ui_map_runtime/include ${REPO}/modules/ui_presentation/include)
add_test(NAME map_annotation_layout_contract COMMAND map_annotation_layout_contract)
add_executable(map_annotation_frame_contract test_annotation_frame.cpp
    ${REPO}/modules/ui_map_runtime/src/map_poi/annotation_frame.cpp
    ${REPO}/modules/ui_map_runtime/src/map_poi/annotation_layout.cpp)
target_include_directories(map_annotation_frame_contract PRIVATE
    ${REPO}/modules/ui_map_runtime/include ${REPO}/modules/ui_presentation/include)
add_test(NAME map_annotation_frame_contract COMMAND map_annotation_frame_contract)
add_executable(map_tile_async_regression
    ${REPO}/modules/ui_map_runtime/tests/test_map_tile_async_runtime.cpp
    ${REPO}/modules/ui_map_runtime/src/map_tiles/map_tile_async_runtime.cpp
    ${REPO}/modules/ui_map_runtime/src/map_tiles/map_tile_render_queue.cpp)
target_include_directories(map_tile_async_regression PRIVATE
    ${REPO}/modules/ui_map_runtime/include ${REPO}/modules/core_sys/include)
add_test(NAME map_tile_async_regression COMMAND map_tile_async_regression)
if(MSVC)
    target_compile_options(map_poi_contract PRIVATE /utf-8 /EHsc)
    target_compile_options(map_annotation_parser_contract PRIVATE /utf-8 /EHsc)
    target_compile_options(map_annotation_layout_contract PRIVATE /utf-8 /EHsc)
    target_compile_options(map_annotation_frame_contract PRIVATE /utf-8 /EHsc)
endif()
