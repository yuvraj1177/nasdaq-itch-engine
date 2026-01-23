cmake_minimum_required(VERSION 3.20)
project(itch_engine VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler flags for performance
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -pedantic")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -march=native -DNDEBUG")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -O0")

# Source files
set(SOURCES
    src/main.cpp
    src/engine.cpp
    src/itch_parser.cpp
    src/order_book.cpp
)

# Executable
add_executable(itch_engine ${SOURCES})

# Include directories
target_include_directories(itch_engine PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

# Link threads library (for future SPSC mode)
find_package(Threads REQUIRED)
target_link_libraries(itch_engine PRIVATE Threads::Threads)

# Enable compile_commands.json for IDE support
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
