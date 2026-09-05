cmake_minimum_required(VERSION 3.10)
project(mylang CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(mylang
    src/main.cpp
    src/lexer/lexer.cpp
    src/parser/parser.cpp
    src/ast/ast_printer.cpp
    src/interpreter/environment.cpp
    src/interpreter/interpreter.cpp
    src/interpreter/user_function.cpp
    src/interpreter/value.cpp
    src/interpreter/lox_class.cpp
    src/interpreter/lox_instance.cpp
    src/stdlib/native_function.cpp
    src/stdlib/math_lib.cpp
    src/stdlib/string_lib.cpp
    src/stdlib/io_lib.cpp
    src/stdlib/array_lib.cpp
    src/stdlib/type_lib.cpp
    src/stdlib/map_lib.cpp
    src/stdlib/stdlib.cpp
)

target_include_directories(mylang PRIVATE src)

# Also copy the built exe to the project root after every build, so
# `.\mylang.exe` at the top level always matches the latest CMake build
# (the "real"/canonical binary still lives in build/Debug/ — this is
# purely a convenience copy).
add_custom_command(TARGET mylang POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:mylang>
        ${CMAKE_SOURCE_DIR}/mylang${CMAKE_EXECUTABLE_SUFFIX}
)