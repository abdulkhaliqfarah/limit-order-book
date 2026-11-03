cmake_minimum_required(VERSION 3.20)
project(limit_order_book CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra -Wpedantic)

add_library(lob src/order_book.cpp)
target_include_directories(lob PUBLIC include)

add_executable(gen_feed tools/gen_feed.cpp)
target_link_libraries(gen_feed PRIVATE lob)

add_executable(replay tools/replay.cpp)
target_link_libraries(replay PRIVATE lob)

add_executable(check tools/check.cpp)
target_link_libraries(check PRIVATE lob)

enable_testing()
add_executable(test_order_book tests/test_order_book.cpp)
target_link_libraries(test_order_book PRIVATE lob)
add_test(NAME order_book_scenarios COMMAND test_order_book)
