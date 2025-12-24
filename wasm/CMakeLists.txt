cmake_minimum_required(VERSION 3.5)
project(quirrel-vscode VERSION 1.0 LANGUAGES C CXX)

# Use CMAKE_CURRENT_SOURCE_DIR for reliable path resolution
set(QUIRREL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../quirrel")

# Add quirrel submodule components
add_subdirectory("${QUIRREL_DIR}/squirrel" "${PROJECT_BINARY_DIR}/squirrel")
add_subdirectory("${QUIRREL_DIR}/squirrel/compiler" "${PROJECT_BINARY_DIR}/quirrel-compiler")

# Create the WASM executable
add_executable(quirrel-vscode native.cpp)
target_link_libraries(quirrel-vscode squirrel quirrel-compiler)

# Include paths for compiler internals (AST access)
target_include_directories(squirrel PUBLIC
    "$<BUILD_INTERFACE:${QUIRREL_DIR}/include>"
    "$<BUILD_INTERFACE:${QUIRREL_DIR}/internal>"
    "$<BUILD_INTERFACE:${QUIRREL_DIR}/helpers>"
    "$<BUILD_INTERFACE:${QUIRREL_DIR}/squirrel>"
)

target_include_directories(quirrel-compiler PUBLIC
    "$<BUILD_INTERFACE:${QUIRREL_DIR}/include>"
    "$<BUILD_INTERFACE:${QUIRREL_DIR}/squirrel>"
    "$<BUILD_INTERFACE:${QUIRREL_DIR}/squirrel/compiler>"
    "$<BUILD_INTERFACE:${QUIRREL_DIR}/helpers>"
)

target_include_directories(quirrel-vscode PUBLIC
    "$<BUILD_INTERFACE:${QUIRREL_DIR}/include>"
    "$<BUILD_INTERFACE:${QUIRREL_DIR}/squirrel>"
    "$<BUILD_INTERFACE:${QUIRREL_DIR}/squirrel/compiler>"
    "$<BUILD_INTERFACE:${QUIRREL_DIR}/helpers>"
)

# Emscripten linker flags
# --bind: Enable embind for C++/JS interop
# -sMODULARIZE=1: Export as a module factory function
# -sEXPORT_ES6=1: Use ES6 module syntax
# -sENVIRONMENT=node: Target Node.js runtime (VS Code)
# -sFILESYSTEM=0: No virtual filesystem needed
# -sALLOW_MEMORY_GROWTH=1: Dynamic memory for large files
# -sEXPORTED_RUNTIME_METHODS: Export UTF8ToString for string handling
# -sNO_DISABLE_EXCEPTION_CATCHING: Enable C++ exception handling (needed for parse errors)
set_target_properties(quirrel-vscode PROPERTIES
    LINK_FLAGS "--bind -sMODULARIZE=1 -sEXPORT_ES6=1 -sENVIRONMENT=node -sFILESYSTEM=0 -sALLOW_MEMORY_GROWTH=1 -sNO_DISABLE_EXCEPTION_CATCHING -sEXPORTED_RUNTIME_METHODS=['UTF8ToString']"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../out"
)
