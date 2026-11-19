cmake_minimum_required(VERSION 3.20)
project(Reclaim VERSION 1.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
endif()

add_library(reclaim_core STATIC
    src/core/sha256.cpp
    src/core/format.cpp
    src/core/json.cpp
    src/core/rules.cpp
    src/core/ntfs_mft.cpp
    src/core/shell_integration.cpp
    src/core/scanner.cpp
    src/core/duplicates.cpp
    src/core/treemap.cpp
    src/core/quarantine.cpp
    src/core/config.cpp
    src/core/reporter.cpp
)
target_include_directories(reclaim_core PUBLIC include)

if(MSVC)
    target_compile_options(reclaim_core PRIVATE /W4 /WX /utf-8 /O2)
else()
    target_compile_options(reclaim_core PRIVATE -Wall -Wextra -Werror -O2)
endif()

add_executable(reclaim-cli src/cli/main_cli.cpp)
target_link_libraries(reclaim-cli PRIVATE reclaim_core)

if(WIN32)
    add_executable(Reclaim WIN32
        src/win32/win_main.cpp
        src/win32/main_window.cpp
        src/win32/reclaim.rc
    )
    target_link_libraries(Reclaim PRIVATE
        reclaim_core
        gdiplus
        dwmapi
        comctl32
        shell32
        ole32
        comdlg32
        advapi32
    )
    target_compile_definitions(Reclaim PRIVATE UNICODE _UNICODE)
    if(MSVC)
        target_compile_options(Reclaim PRIVATE /W4 /utf-8 /O2)
    else()
        target_compile_options(Reclaim PRIVATE -Wall -Wextra -O2 -municode)
    endif()
endif()

enable_testing()
add_executable(reclaim_tests tests/test_suite.cpp)
target_link_libraries(reclaim_tests PRIVATE reclaim_core)
add_test(NAME reclaim_unit_integration COMMAND reclaim_tests)
