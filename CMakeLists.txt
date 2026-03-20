cmake_minimum_required(VERSION 3.20)
project(itch_engine VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -pedantic")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -march=native -DNDEBUG")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -O0")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# ---------------------------------------------------------------------------
# Main engine executable
# ---------------------------------------------------------------------------
set(ENGINE_SOURCES
    src/main.cpp
    src/engine.cpp
    src/itch_parser.cpp
    src/order_book.cpp
    src/l3_csv_exporter.cpp
)

add_executable(itch_engine ${ENGINE_SOURCES})

target_include_directories(itch_engine PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

find_package(Threads REQUIRED)
target_link_libraries(itch_engine PRIVATE Threads::Threads)

# ---------------------------------------------------------------------------
# Replay / validation tool
# ---------------------------------------------------------------------------
add_executable(replay_csv tools/replay_csv.cpp)

target_include_directories(replay_csv PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# ---------------------------------------------------------------------------
# IDE support
# ---------------------------------------------------------------------------
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
