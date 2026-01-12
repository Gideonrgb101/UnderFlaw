cmake_minimum_required(VERSION 3.10)
project(ChessEngine C)

# Set C standard
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Set build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Project version
set(PROJECT_VERSION 2.0)
set(PROJECT_DESCRIPTION "Advanced C Chess Engine with Phase 1 & Phase 2 Optimizations")

# Source files
set(SOURCES
    src/main.c
    src/search.c
    src/evaluation.c
    src/movegen.c
    src/movepicker.c
    src/position.c
    src/tt.c
    src/uci.c
    src/book.c
    src/cache.c
    src/tablebase.c
    src/threads.c
    src/tuner.c
    src/magic.c
    src/perft.c
    src/nnue.c
    src/see.c
    external/fathom/tbprobe.c
)

# Include directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/external/fathom)

# Create executable
add_executable(UnderFlaw ${SOURCES})

# Platform-specific settings
if(MSVC)
    # Windows MSVC
    target_compile_options(UnderFlaw PRIVATE /W4 /O2)
    target_link_libraries(UnderFlaw PRIVATE m)
elseif(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    # GCC/Clang on Unix-like systems
    target_compile_options(UnderFlaw PRIVATE 
        -Wall -Wextra -Wpedantic
        -march=native
        -O3
    )
    target_link_libraries(UnderFlaw PRIVATE m pthread)
    
    # Link-time optimization disabled for stability
    # set_property(TARGET UnderFlaw PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

# Output directory
set_target_properties(UnderFlaw PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# Install target
install(TARGETS UnderFlaw
    RUNTIME DESTINATION bin
)

# Install headers
install(DIRECTORY include/
    DESTINATION include
    FILES_MATCHING PATTERN "*.h"
)

# Install documentation
install(DIRECTORY docs/
    DESTINATION share/doc/chess_engine
)

# Install assets
install(DIRECTORY assets/
    DESTINATION share/chess_engine
)

# Print configuration
message(STATUS "Chess Engine ${PROJECT_VERSION}")
message(STATUS "Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C Compiler: ${CMAKE_C_COMPILER}")
message(STATUS "Source files: ${SOURCES}")

