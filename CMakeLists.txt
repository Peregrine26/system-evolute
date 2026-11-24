cmake_minimum_required(VERSION 3.20)

project(CrossDesk VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(CROSSDESK_BUILD_GUI "Build the Qt desktop application" ON)
option(CROSSDESK_BUILD_SCANNER "Build the Windows scanner example" ${WIN32})
include(CTest)

if(CROSSDESK_BUILD_GUI)
    find_package(Qt6 6.2 REQUIRED COMPONENTS Widgets Network Concurrent)

    add_library(crossdesk_desktop STATIC
        src/desktop/store.h src/desktop/store.cpp
        src/desktop/pages.h src/desktop/pages.cpp
        src/desktop/workbench.h src/desktop/workbench.cpp
    )
    set_target_properties(crossdesk_desktop PROPERTIES AUTOMOC ON)
    target_include_directories(crossdesk_desktop PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(crossdesk_desktop PUBLIC Qt6::Widgets Qt6::Network Qt6::Concurrent)
    if(WIN32)
        target_sources(crossdesk_desktop PRIVATE src/platform/windows/desktop_shell.cpp src/platform/windows/global_shortcuts.cpp)
        target_link_libraries(crossdesk_desktop PRIVATE user32 shell32 ole32 uuid advapi32 dwmapi)
        target_compile_definitions(crossdesk_desktop PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    else()
        target_sources(crossdesk_desktop PRIVATE src/desktop/shell_stub.cpp)
    endif()
    if(MSVC)
        target_compile_options(crossdesk_desktop PRIVATE /utf-8)
    endif()

    add_executable(crossdesk WIN32
        src/app/main.cpp
    )
    target_link_libraries(crossdesk PRIVATE crossdesk_desktop)
    if(WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 8)
        if(CMAKE_GENERATOR_PLATFORM STREQUAL "ARM64" OR CMAKE_SYSTEM_PROCESSOR MATCHES "^(ARM64|aarch64)$")
            set(CROSSDESK_EVERYTHING_DLL EverythingARM64.dll)
        else()
            set(CROSSDESK_EVERYTHING_DLL Everything64.dll)
        endif()
        add_custom_command(TARGET crossdesk POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/third_party/everything/${CROSSDESK_EVERYTHING_DLL}"
                "$<TARGET_FILE_DIR:crossdesk>/EverythingSdk.dll"
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:crossdesk>/licenses/Everything"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/third_party/everything/LICENSE.txt"
                "$<TARGET_FILE_DIR:crossdesk>/licenses/Everything/LICENSE.txt")
    endif()
    if(BUILD_TESTING)
        add_executable(crossdesk_desktop_store_test tests/desktop_store_test.cpp)
        target_link_libraries(crossdesk_desktop_store_test PRIVATE crossdesk_desktop)
        if(MSVC)
            target_compile_options(crossdesk_desktop_store_test PRIVATE /utf-8)
        endif()
        add_test(NAME desktop_store_test COMMAND crossdesk_desktop_store_test)
        add_executable(crossdesk_desktop_ui_test tests/desktop_ui_test.cpp)
        target_link_libraries(crossdesk_desktop_ui_test PRIVATE crossdesk_desktop)
        if(MSVC)
            target_compile_options(crossdesk_desktop_ui_test PRIVATE /utf-8)
        endif()
        add_test(NAME desktop_ui_test COMMAND crossdesk_desktop_ui_test)
        set_tests_properties(desktop_ui_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;CROSSDESK_OFFLINE=1" TIMEOUT 15)
        add_test(NAME desktop_app_smoke COMMAND crossdesk --smoke)
        set_tests_properties(desktop_app_smoke PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen;CROSSDESK_OFFLINE=1" TIMEOUT 15)
    endif()
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
        src/window_catalog.cpp
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
        add_executable(crossdesk_window_catalog_test tests/window_catalog_test.cpp)
        target_link_libraries(crossdesk_window_catalog_test PRIVATE crossdesk_window_scanner_lib)
        add_test(NAME window_catalog_test COMMAND crossdesk_window_catalog_test)

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
    target_compile_definitions(crossdesk PRIVATE CROSSDESK_WITH_WINDOW_API)
    target_sources(crossdesk_desktop PRIVATE src/desktop/window_overview.cpp)
    target_link_libraries(crossdesk_desktop PRIVATE crossdesk_qt_platform crossdesk_window_scanner_lib)
    add_executable(crossdesk_qt_dwm_demo src/app/qt_dwm_demo.cpp)
    target_link_libraries(crossdesk_qt_dwm_demo PRIVATE crossdesk_qt_platform)
    if(BUILD_TESTING)
        add_executable(crossdesk_qt_bridge_lifecycle_test tests/qt_bridge_lifecycle_test.cpp)
        target_link_libraries(crossdesk_qt_bridge_lifecycle_test PRIVATE crossdesk_qt_platform)
        target_compile_definitions(crossdesk_qt_bridge_lifecycle_test PRIVATE
            UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
        add_test(NAME qt_bridge_lifecycle_test COMMAND crossdesk_qt_bridge_lifecycle_test)
        set_tests_properties(qt_bridge_lifecycle_test PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 15)
        add_test(NAME qt_dwm_smoke COMMAND crossdesk_qt_dwm_demo --smoke)
        set_tests_properties(qt_dwm_smoke PROPERTIES
            LABELS "interactive-desktop" TIMEOUT 15)
        add_executable(crossdesk_pid_filter_selftest tests/pid_filter_selftest.cpp)
        target_link_libraries(crossdesk_pid_filter_selftest PRIVATE crossdesk_desktop crossdesk_win32_backend crossdesk_window_scanner_lib)
        add_test(NAME pid_filter_selftest COMMAND crossdesk_pid_filter_selftest)
        set_tests_properties(pid_filter_selftest PROPERTIES LABELS "interactive-desktop" TIMEOUT 15)
    endif()
endif()
