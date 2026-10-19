cmake_minimum_required(VERSION 3.20)

project(CrossDesk VERSION 0.1.0 LANGUAGES CXX)

enable_testing()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Qt 的 signals/slots 需要 moc 预处理器。没有这一行，Q_OBJECT 类会在链接阶段
# 报 "undefined reference to vtable"，这是 Qt 新手第一个必踩的坑。
set(CMAKE_AUTOMOC ON)

option(CROSSDESK_BUILD_GUI "Build the Qt desktop application" ON)
option(CROSSDESK_BUILD_SCANNER "Build the Windows scanner example" ${WIN32})
option(CROSSDESK_BUILD_UI_SMOKE "Build the headless UI smoke test" ON)

if(CROSSDESK_BUILD_GUI)
    find_package(Qt6 6.2 REQUIRED COMPONENTS Widgets)

    # UI 单独成库，这样交付程序和无界面冒烟测试编译的是同一份代码。
    add_library(crossdesk_ui STATIC
        src/ui/overview_types.h
        src/ui/workspace_service.h
        src/ui/fake_workspace_service.cpp
        src/ui/fake_workspace_service.h
        src/ui/overview_window.cpp
        src/ui/overview_window.h
    )
    target_include_directories(crossdesk_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(crossdesk_ui PUBLIC Qt6::Widgets)

    add_executable(crossdesk WIN32
        src/app/main.cpp
    )
    target_link_libraries(crossdesk PRIVATE crossdesk_ui)

    if(CROSSDESK_BUILD_UI_SMOKE)
        add_executable(crossdesk_ui_smoke tests/ui_smoke.cpp)
        target_link_libraries(crossdesk_ui_smoke PRIVATE crossdesk_ui)
        add_test(NAME ui_smoke COMMAND crossdesk_ui_smoke)
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
