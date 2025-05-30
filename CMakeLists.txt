cmake_minimum_required(VERSION 3.5)
project (basic_coro VERSION 1.0.0)
if (MSVC)
	add_compile_options(/W4 /EHsc /DNOMINMAX /std:c++20)
	set(STANDARD_LIBRARIES "")
else()
	add_compile_options(-Wall -Wextra -Wpedantic -std=c++20)
	set(STANDARD_LIBRARIES "pthread")
endif()


if (CMAKE_COMPILER_IS_GNUCC AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 11.0)
    add_compile_options(-fcoroutines)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
	set(CMAKE_INSTALL_PREFIX "/usr/local" CACHE PATH "Default path to install" FORCE)
endif()

include_directories(AFTER ${CMAKE_CURRENT_LIST_DIR}/src)


enable_testing()
add_subdirectory("src/tests")

