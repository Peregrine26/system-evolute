cmake_minimum_required(VERSION 3.20)

project(CrossDesk VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(CROSSDESK_BUILD_GUI "Build the Qt desktop application" ON)
option(CROSSDESK_BUILD_SCANNER "Build the Windows scanner example" ${WIN32})

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
endif()
