cmake_minimum_required(VERSION 3.20)
project(astar_research LANGUAGES CXX)

find_package(OpenMP REQUIRED)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

add_library(astar_core
    src/graph.cpp
    src/grid.cpp
    src/heuristic.cpp
    src/oracle.cpp
    src/search.cpp
    src/experiments.cpp
    src/preprocess.cpp
    src/novelty.cpp
    src/omp.cpp
)
target_include_directories(astar_core PUBLIC include)
target_compile_options(astar_core PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(astar_core PUBLIC OpenMP::OpenMP_CXX)

add_executable(astar_research src/main.cpp)
target_link_libraries(astar_research PRIVATE astar_core)

add_executable(test_core tests/test_core.cpp)
target_link_libraries(test_core PRIVATE astar_core)

enable_testing()
add_test(NAME core COMMAND test_core)
