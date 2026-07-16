cmake_minimum_required(VERSION 3.16)
project(quark LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(ME_ENABLE_LTO "Enable link-time optimization in Release" ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

add_library(me_engine
  src/order_book.cpp
)
target_include_directories(me_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU|AppleClang")
  target_compile_options(me_engine PRIVATE
    -Wall -Wextra -Wpedantic
    $<$<CONFIG:Release>:-O3 -DNDEBUG>
    $<$<CONFIG:Release>:-march=native>
  )
  if(ME_ENABLE_LTO AND CMAKE_BUILD_TYPE STREQUAL "Release")
    include(CheckIPOSupported)
    check_ipo_supported(RESULT ipo_ok OUTPUT ipo_err)
    if(ipo_ok)
      set_property(TARGET me_engine PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
    endif()
  endif()
endif()

add_executable(me_tests tests/unit_tests.cpp)
target_link_libraries(me_tests PRIVATE me_engine)
target_include_directories(me_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_executable(me_bench bench/benchmark.cpp)
target_link_libraries(me_bench PRIVATE me_engine)
target_include_directories(me_bench PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU|AppleClang")
  target_compile_options(me_tests PRIVATE -O2)
  target_compile_options(me_bench PRIVATE -O3 -DNDEBUG -march=native)
endif()

enable_testing()
add_test(NAME unit_tests COMMAND me_tests)

install(TARGETS me_engine me_tests me_bench
  RUNTIME DESTINATION bin
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib
)
