cmake_minimum_required(VERSION 3.10)
project(toy-compiler)

set(CMAKE_CXX_STANDARD 14)

file(GLOB CPPS ${CMAKE_SOURCE_DIR}/*.cpp)

# Find Flex and Bison packages
find_package(FLEX REQUIRED)
find_package(BISON REQUIRED)

# Define the Flex and Bison input and output files
flex_target(Scanner tokens.l ${CMAKE_SOURCE_DIR}/tokens.cpp)
bison_target(Parser parser.y ${CMAKE_SOURCE_DIR}/parser.cpp)
add_flex_bison_dependency(Scanner Parser)

# Use customized LLVM
find_package(Clang REQUIRED CONFIG HINTS ${LLVM_DIR} NO_DEFAULT_PATH)
find_package(LLVM REQUIRED CONFIG HINTS ${LLVM_DIR} NO_DEFAULT_PATH)

# Use default LLVM
# find_package(Clang REQUIRED CONFIG)
# find_package(LLVM REQUIRED CONFIG)

include_directories(${LLVM_INCLUDE_DIRS} ${CLANG_INCLUDE_DIRS} SYSTEM)
link_directories(${LLVM_LIBRARY_DIRS})

# Add the executable
add_executable(compiler
        ${CPPS}
        ${BISON_Parser_OUTPUTS}
        ${FLEX_Scanner_OUTPUTS}
        )

target_link_libraries(compiler
        LLVMX86AsmParser
        LLVMX86CodeGen
        LLVMAsmParser
        LLVMBitWriter
        LLVMRuntimeDyld
        LLVMExecutionEngine
        LLVMMCJIT
        z
        ncurses
        )