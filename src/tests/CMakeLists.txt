set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests/)

set(testFiles all_compile.cpp
              coro_and_awaiter.cpp
              anyof_allof.cpp
              generator.cpp
              mutex.cpp
              distributor.cpp
              scheduler.cpp
              scheduler_cycle.cpp
              queue.cpp
              flat_stack_alloc.cpp
              )

foreach (testFile ${testFiles})
    string(REGEX MATCH "([^\/]+$)" filename ${testFile})
    string(REGEX MATCH "[^.]*" executable_name test_${filename})
    add_executable(${executable_name} ${testFile})
    target_link_libraries(${executable_name} ${STANDARD_LIBRARIES} )
    add_test(NAME ${executable_name} COMMAND ${executable_name})
endforeach ()
