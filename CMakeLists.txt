cmake_minimum_required(VERSION 3.20)

project(CrossDesk VERSION 0.1.0 LANGUAGES CXX)

enable_testing()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 源码里带中文注释和中文界面文案，全部按 UTF-8 保存。
# MSVC 默认按系统代码页（简体中文环境是 GBK）读源文件，会把 UTF-8 的多字节
# 序列拆错，严重时一个中文字节会把行尾的换行吃掉，让下一行代码并进注释，
# 表现为莫名其妙的 "语法错误: public"。
if(MSVC)
    add_compile_options(/utf-8)
endif()

# Qt 的 signals/slots 需要 moc 预处理器。没有这一行，Q_OBJECT 类会在链接阶段
# 报 "undefined reference to vtable"，这是 Qt 新手第一个必踩的坑。
set(CMAKE_AUTOMOC ON)

option(CROSSDESK_BUILD_GUI "Build the Qt desktop application" ON)
option(CROSSDESK_BUILD_SCANNER "Build the Windows scanner example" ${WIN32})
option(CROSSDESK_BUILD_UI_SMOKE "Build the headless UI smoke test" ON)
include(CTest)

if(CROSSDESK_BUILD_GUI)
    find_package(Qt6 6.2 REQUIRED COMPONENTS Widgets)

    # UI 单独成库，这样交付程序和无界面冒烟测试编译的是同一份代码。
    add_library(crossdesk_ui STATIC
        src/ui/window_item.h
        src/ui/layout_math.h
        src/ui/layout_math.cpp
        src/ui/crossdesk_service.h
        src/ui/fake_service.h
        src/ui/fake_service.cpp
        src/ui/preview_widgets.h
        src/ui/preview_widgets.cpp
        src/ui/window_cards.h
        src/ui/window_cards.cpp
        src/ui/main_window.h
        src/ui/main_window.cpp
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
        set_tests_properties(ui_smoke PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 20)
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
        add_executable(crossdesk_win32_commands_test tests/win32_commands_test.cpp
            src/platform/windows/win32_window_commands.cpp)
        target_include_directories(crossdesk_win32_commands_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
        target_link_libraries(crossdesk_win32_commands_test PRIVATE user32)
        add_test(NAME win32_commands_test COMMAND crossdesk_win32_commands_test)
        set_tests_properties(win32_commands_test PROPERTIES TIMEOUT 15)

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
    endif()
endif()

# 真实窗口数据源：把 A 的扫描器接到界面上，显示真实窗口。
# 查询走 A 的扫描器；命令（置前 / 设位置）走 win32_window_commands，
# 那是 A 的地盘上的兜底实现，A 接好适配器后删掉即可。
if(CROSSDESK_BUILD_GUI AND WIN32 AND CROSSDESK_BUILD_SCANNER)
    target_sources(crossdesk_ui PRIVATE
        src/ui/live_service.cpp
        src/ui/live_service.h
        src/platform/windows/win32_window_commands.cpp
        src/platform/windows/win32_window_commands.h
    )
    target_link_libraries(crossdesk_ui PUBLIC
        crossdesk_window_scanner_lib
        crossdesk_win32_backend
    )
    # PUBLIC：main.cpp 也要看见这个宏才知道 --live 是否可用
    target_compile_definitions(crossdesk_ui PUBLIC CROSSDESK_HAS_LIVE_SCANNER=1)

    # 平台命令层的真机测试：启动一个真实 crossdesk.exe 当靶子，
    # 验证 place() 真的改变了窗口矩形、activate() 真的把前台切过去。
    if(CROSSDESK_BUILD_UI_SMOKE)
        add_executable(crossdesk_win32_commands_smoke
            tests/win32_commands_smoke.cpp
            src/platform/windows/win32_window_commands.cpp
        )
        target_include_directories(crossdesk_win32_commands_smoke PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src
        )
        add_test(NAME win32_commands_smoke COMMAND crossdesk_win32_commands_smoke)
        add_dependencies(crossdesk_win32_commands_smoke crossdesk)
        set_tests_properties(win32_commands_smoke PROPERTIES LABELS "interactive-desktop" TIMEOUT 20)
        add_executable(crossdesk_live_service_test tests/live_service_test.cpp)
        target_link_libraries(crossdesk_live_service_test PRIVATE crossdesk_ui)
        add_test(NAME live_service_test COMMAND crossdesk_live_service_test)
        set_tests_properties(live_service_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 15)
        add_executable(crossdesk_ui_dwm_selftest tests/ui_dwm_selftest.cpp)
        target_link_libraries(crossdesk_ui_dwm_selftest PRIVATE crossdesk_ui crossdesk_qt_platform)
        add_test(NAME ui_dwm_selftest COMMAND crossdesk_ui_dwm_selftest)
        set_tests_properties(ui_dwm_selftest PROPERTIES LABELS "interactive-desktop" TIMEOUT 15)
    endif()
endif()
