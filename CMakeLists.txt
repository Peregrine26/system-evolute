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
    endif()
endif()
