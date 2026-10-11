cmake_minimum_required(VERSION 3.14)
project(emu6502 CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(cpu6502 STATIC src/cpu.cpp)
target_include_directories(cpu6502 PUBLIC src)

add_executable(emu6502 src/main.cpp)
target_link_libraries(emu6502 PRIVATE cpu6502)
if(MINGW)
    # No runtime DLLs needed, so the .exe runs when double-clicked.
    target_link_options(emu6502 PRIVATE -static)
endif()

enable_testing()
add_executable(tests tests/tests.cpp)
target_link_libraries(tests PRIVATE cpu6502)
add_test(NAME tests COMMAND tests)
