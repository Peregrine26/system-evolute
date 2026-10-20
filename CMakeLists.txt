cmake_minimum_required(VERSION 3.20)

project(CrossDesk VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(CROSSDESK_BUILD_GUI "Build the Qt desktop application" ON)
option(CROSSDESK_BUILD_SCANNER "Build the Windows scanner example" ${WIN32})
option(CROSSDESK_BUILD_TESTS "Build the automated tests" ON)

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

# Core logic (B): filtering, selection validation and layout. Pure C++17, no
# Win32 and no Qt, so it builds and its tests run on any platform.
add_library(crossdesk_core STATIC
    src/core/window_filter.cpp
    src/core/window_layout.cpp
    src/core/window_selection.cpp
)
target_link_libraries(crossdesk_core PUBLIC crossdesk_window_api)

# Source files contain Chinese user-facing messages; without /utf-8 MSVC would
# read them in the system code page and mangle the literals.
if(MSVC)
    target_compile_options(crossdesk_core PRIVATE /utf-8)
endif()

if(CROSSDESK_BUILD_TESTS)
    enable_testing()

    add_executable(crossdesk_core_tests
        tests/core_tests.cpp
    )
    target_link_libraries(crossdesk_core_tests PRIVATE crossdesk_core)
    if(MSVC)
        target_compile_options(crossdesk_core_tests PRIVATE /utf-8)
    endif()
    add_test(NAME core_tests COMMAND crossdesk_core_tests)
endif()

if(CROSSDESK_BUILD_SCANNER)
    if(NOT WIN32)
        message(FATAL_ERROR "The scanner example currently requires Windows.")
    endif()

    add_library(crossdesk_window_scanner_lib STATIC
        src/windows_window_scanner.cpp
    )
    target_link_libraries(crossdesk_window_scanner_lib PUBLIC crossdesk_window_api)

    add_library(crossdesk_win32_backend STATIC
        src/platform/windows/win32_api_adapter.cpp
    )
    target_link_libraries(crossdesk_win32_backend
        PUBLIC crossdesk_window_api
        PRIVATE user32
    )
    target_compile_definitions(crossdesk_win32_backend PRIVATE
        UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN
    )

    add_executable(crossdesk_window_scanner
        src/main.cpp
    )
    target_link_libraries(crossdesk_window_scanner PRIVATE
        crossdesk_window_scanner_lib
        crossdesk_win32_backend
    )

    # A -> B seam: the real scanner driven by a fake adapter.
    if(CROSSDESK_BUILD_TESTS)
        add_executable(crossdesk_pipeline_tests
            tests/pipeline_tests.cpp
        )
        target_link_libraries(crossdesk_pipeline_tests PRIVATE
            crossdesk_core
            crossdesk_window_scanner_lib
        )
        if(MSVC)
            target_compile_options(crossdesk_pipeline_tests PRIVATE /utf-8)
        endif()
        add_test(NAME pipeline_tests COMMAND crossdesk_pipeline_tests)
    endif()
endif()
