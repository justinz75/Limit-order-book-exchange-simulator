cmake_minimum_required(VERSION 3.15)

project(limit_order_book CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

#default to a release build, since the benchmark numbers are meaningless without optimisation
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

#the matching engine on its own, with no dependency on the simulation around it
add_library(order_book src/order_book.cpp)
target_include_directories(order_book PUBLIC include)

#the random order flow and the csv writer that records it
add_library(simulation src/simulator.cpp data/data_writer.cpp)
target_include_directories(simulation PUBLIC data)
target_link_libraries(simulation PUBLIC order_book)

#writes data/simulation.csv, which analysis/analysis.py then reads
add_executable(simulator src/simulator_main.cpp)
target_link_libraries(simulator PRIVATE simulation)

add_executable(benchmark benchmarks/benchmark.cpp)
target_link_libraries(benchmark PRIVATE order_book)

enable_testing()

add_executable(order_book_tests tests/order_book_tests.cpp)
target_link_libraries(order_book_tests PRIVATE order_book)
target_include_directories(order_book_tests PRIVATE tests)
add_test(NAME order_book_tests COMMAND order_book_tests)

add_executable(simulator_tests tests/simulator_tests.cpp)
target_link_libraries(simulator_tests PRIVATE simulation)
target_include_directories(simulator_tests PRIVATE tests)
add_test(NAME simulator_tests COMMAND simulator_tests)
