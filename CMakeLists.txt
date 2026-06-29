cmake_minimum_required(VERSION 3.25)
project(HardCap VERSION 0.3.1 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT MSVC)
    message(FATAL_ERROR "HardCap requires MSVC on Windows")
endif()

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_library(hardcap_core STATIC
    src/core/rule.cpp
    src/core/rule_repository.cpp
    src/engine/process_groups.cpp
    src/platform/job_controller.cpp
    src/platform/process_monitor.cpp
    src/platform/unelevated_launcher.cpp
    src/engine/rule_engine.cpp
)
target_include_directories(hardcap_core PUBLIC src)
target_compile_definitions(hardcap_core PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(hardcap_core PRIVATE /W4 /permissive- /EHsc /utf-8)
target_link_libraries(hardcap_core PUBLIC advapi32 ole32 oleaut32 psapi shell32 userenv wbemuuid)

add_executable(HardCap WIN32
    src/app/main.cpp
    src/ui/main_window.cpp
    src/ui/tray_controller.cpp
    resources/HardCap.rc
)
target_include_directories(HardCap PRIVATE src resources)
target_compile_definitions(HardCap PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(HardCap PRIVATE /W4 /permissive- /EHsc /utf-8)
target_link_libraries(HardCap PRIVATE hardcap_core comctl32 d2d1 dwrite dwmapi shlwapi uxtheme windowscodecs)
target_link_options(HardCap PRIVATE /MANIFEST:NO)
set_property(TARGET HardCap PROPERTY VS_DPI_AWARE "PerMonitor")
set_target_properties(HardCap PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/dist"
    PDB_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/symbols"
    COMPILE_PDB_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/symbols"
)

if(CMAKE_BUILD_TYPE STREQUAL "Release" OR CMAKE_CONFIGURATION_TYPES)
    set_property(TARGET HardCap PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
endif()

enable_testing()
add_executable(hardcap_tests tests/test_main.cpp)
target_include_directories(hardcap_tests PRIVATE src)
target_compile_definitions(hardcap_tests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(hardcap_tests PRIVATE /W4 /permissive- /EHsc /utf-8)
target_link_libraries(hardcap_tests PRIVATE hardcap_core)
add_test(NAME hardcap_tests COMMAND hardcap_tests)

add_executable(hardcap_test_helper tests/test_helper.cpp)
target_compile_definitions(hardcap_test_helper PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(hardcap_test_helper PRIVATE /W4 /permissive- /EHsc /utf-8)
add_dependencies(hardcap_tests hardcap_test_helper)

add_executable(hardcap_ui_smoke WIN32
    tests/ui_smoke_main.cpp
    src/ui/main_window.cpp
    src/ui/tray_controller.cpp
)
target_include_directories(hardcap_ui_smoke PRIVATE src resources)
target_compile_definitions(hardcap_ui_smoke PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(hardcap_ui_smoke PRIVATE /W4 /permissive- /EHsc /utf-8)
target_link_libraries(hardcap_ui_smoke PRIVATE hardcap_core comctl32 d2d1 dwrite dwmapi shlwapi uxtheme windowscodecs)
