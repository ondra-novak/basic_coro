cmake_minimum_required(VERSION 3.16)
project (basic_coro VERSION 1.0.0)

add_library(basic_coro INTERFACE)
add_library(BasicCoro::basic_coro ALIAS basic_coro)

target_compile_features(basic_coro INTERFACE cxx_std_20)

target_include_directories(basic_coro INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
  $<INSTALL_INTERFACE:include>
)

enable_testing()
add_subdirectory("tests")

