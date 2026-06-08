cmake_minimum_required(VERSION 3.21)
project (basic_coro VERSION 1.0.0)

add_library(basic_coro INTERFACE)
add_library(basic_coro::basic_coro ALIAS basic_coro)

target_compile_features(basic_coro INTERFACE cxx_std_20)

target_include_directories(basic_coro INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
  $<INSTALL_INTERFACE:include>
)

option(basic_coro_DEVELOPMENT_MODE "(basic_coro) Include development targets" ${PROJECT_IS_TOP_LEVEL})


if (basic_coro_DEVELOPMENT_MODE) 
  enable_testing()
  add_subdirectory("tests")
endif()

