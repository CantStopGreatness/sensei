cmake_minimum_required(VERSION 3.16)
project(ChessEngine CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
add_compile_options(-Wall -Wextra)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(core src/bitboard.cpp src/position.cpp src/movegen.cpp src/perft.cpp)
target_include_directories(core PUBLIC src)

add_executable(engine src/main.cpp)
target_link_libraries(engine core)

enable_testing()
add_executable(perft_tests tests/perft_tests.cpp)
target_link_libraries(perft_tests core)
add_test(NAME perft COMMAND perft_tests)
