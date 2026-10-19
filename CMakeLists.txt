cmake_minimum_required(VERSION 3.20)

project(CursorUsageMonitor VERSION 1.0.0 LANGUAGES C CXX RC)

if(NOT MSVC)
    message(FATAL_ERROR "CursorUsageMonitor is a TrafficMonitor plugin and must be built with MSVC.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

add_library(CursorUsageMonitor SHARED
    src/Session.cpp
    src/UsageClient.cpp
    src/DataManager.cpp
    src/UsageItem.cpp
    src/Lang.cpp
    src/Plugin.cpp
    src/Theme.hpp
    src/CursorUsageMonitor.rc
    third_party/sqlite3.c
)

target_include_directories(CursorUsageMonitor PRIVATE src)
target_include_directories(CursorUsageMonitor SYSTEM PRIVATE
    include
    third_party
)

target_compile_definitions(CursorUsageMonitor PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _WIN32_WINNT=0x0A00
    SQLITE_THREADSAFE=1
    SQLITE_OMIT_LOAD_EXTENSION
    SQLITE_DEFAULT_MEMSTATUS=0
)

target_compile_options(CursorUsageMonitor PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:/utf-8 /W4 /permissive- /EHsc>
    $<$<COMPILE_LANGUAGE:C>:/utf-8 /W0>
)

target_link_libraries(CursorUsageMonitor PRIVATE
    winhttp
    shell32
    ole32
    user32
    gdi32
    advapi32
    dwmapi
    uxtheme
    gdiplus
)
