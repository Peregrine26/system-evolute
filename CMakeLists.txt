cmake_minimum_required(VERSION 3.20)

project(CrossDesk VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(CROSSDESK_BUILD_GUI "Build the Qt desktop application" ON)
option(CROSSDESK_BUILD_SCANNER "Build the Windows scanner example" ${WIN32})
include(CTest)

if(CROSSDESK_BUILD_GUI)
    find_package(Qt6 6.2 REQUIRED COMPONENTS Widgets)

    add_executable(crossdesk WIN32
        src/app/main.cpp
    )
    target_link_libraries(crossdesk PRIVATE Qt6::Widgets)
endif()

# This target exposes only platform-neutral headers to future core modules.
add_library(crossdesk_window_api INTERFACE)
target_include_directories(crossdesk_window_api INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

if(CROSSDESK_BUILD_SCANNER)
    if(NOT WIN32)
        message(FATAL_ERROR "The scanner example currently requires Windows.")
    endif()

    add_library(crossdesk_window_scanner_lib STATIC
        src/windows_window_scanner.cpp
        src/platform/layout_executor.cpp
    )
    target_link_libraries(crossdesk_window_scanner_lib PUBLIC crossdesk_window_api)

    add_library(crossdesk_win32_backend STATIC
        src/platform/windows/win32_api_adapter.cpp
        src/platform/windows/desktop_services.cpp
    )
    target_link_libraries(crossdesk_win32_backend
        PUBLIC crossdesk_window_api
        PRIVATE user32 dwmapi
    )
    target_compile_definitions(crossdesk_win32_backend PRIVATE
        UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN
    )

    add_executable(crossdesk_window_scanner
        src/main.cpp
    )
    if(MINGW)
        # The scanner uses wmain so titles stay Unicode on Windows.
        target_link_options(crossdesk_window_scanner PRIVATE -municode)
    endif()
    add_executable(crossdesk_dwm_demo src/dwm_demo.cpp)
    target_link_libraries(crossdesk_dwm_demo PRIVATE crossdesk_win32_backend user32 gdi32)
    target_compile_definitions(crossdesk_dwm_demo PRIVATE
        UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(crossdesk_window_scanner PRIVATE
        crossdesk_window_scanner_lib
        crossdesk_win32_backend
    )

    if(BUILD_TESTING)
        add_executable(crossdesk_layout_executor_test tests/layout_executor_test.cpp)
        target_link_libraries(crossdesk_layout_executor_test PRIVATE crossdesk_window_scanner_lib)
        add_test(NAME layout_executor_test COMMAND crossdesk_layout_executor_test)

        add_executable(crossdesk_win32_backend_selftest
            tests/win32_backend_selftest.cpp
        )
        target_link_libraries(crossdesk_win32_backend_selftest PRIVATE
            crossdesk_win32_backend
            user32
        )
        add_test(NAME win32_backend_selftest
            COMMAND crossdesk_win32_backend_selftest
        )
        add_executable(crossdesk_desktop_services_selftest tests/desktop_services_selftest.cpp)
        target_link_libraries(crossdesk_desktop_services_selftest PRIVATE crossdesk_win32_backend user32)
        target_compile_definitions(crossdesk_desktop_services_selftest PRIVATE
            UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
        add_test(NAME desktop_services_selftest COMMAND crossdesk_desktop_services_selftest)
        set_tests_properties(win32_backend_selftest desktop_services_selftest PROPERTIES
            LABELS "interactive-desktop")
    endif()
endif()

if(WIN32 AND CROSSDESK_BUILD_GUI AND CROSSDESK_BUILD_SCANNER)
    add_library(crossdesk_qt_platform STATIC src/platform/windows/qt_desktop_bridge.cpp)
    target_link_libraries(crossdesk_qt_platform PUBLIC Qt6::Widgets crossdesk_win32_backend)
    target_compile_definitions(crossdesk_qt_platform PRIVATE
        UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(crossdesk PRIVATE crossdesk_qt_platform)
    add_executable(crossdesk_qt_dwm_demo src/app/qt_dwm_demo.cpp)
    target_link_libraries(crossdesk_qt_dwm_demo PRIVATE crossdesk_qt_platform)
    if(BUILD_TESTING)
        add_test(NAME qt_dwm_smoke COMMAND crossdesk_qt_dwm_demo --smoke)
        set_tests_properties(qt_dwm_smoke PROPERTIES
            LABELS "interactive-desktop" TIMEOUT 15)
    endif()
endif()
