cmake_minimum_required(VERSION 3.13)

# List all files containing tests. (Change as needed)
set(TESTFILES
  runner.c
  src/test_jp2.c
  src/test_jpg.c
  src/test_load.c
  )

set(TEST_MAIN tests)
set(TEST_RUNNER_PARAMS "")

add_executable(${TEST_MAIN} ${TESTFILES})
target_compile_definitions(${TEST_MAIN} PRIVATE
  IM_TEST_DATA="${CMAKE_CURRENT_LIST_DIR}/data"
  IM_TEST_TMP="${CMAKE_CURRENT_BINARY_DIR}"
  )

target_link_libraries(${TEST_MAIN} PRIVATE ${PROJECT_NAME})

# a format's tests where it's built in
foreach(f ${IM_ON})
  string(TOUPPER ${f} F)
  target_compile_definitions(${TEST_MAIN} PRIVATE IM_WITH_${F}=1)
endforeach()
target_include_directories(${TEST_MAIN} PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/include
  ${CMAKE_CURRENT_LIST_DIR}/src
  )

set_target_properties(${TEST_MAIN} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})

if(CMAKE_SYSTEM_NAME STREQUAL "WASI")
  # the runner's clock(), WASI's emulated one; a decoder's stack past
  # wasm-ld's 64 KB
  target_compile_definitions(${TEST_MAIN} PRIVATE _WASI_EMULATED_PROCESS_CLOCKS)
  target_link_libraries(${TEST_MAIN} PRIVATE wasi-emulated-process-clocks)
  target_link_options(${TEST_MAIN} PRIVATE -Wl,-z,stack-size=1048576)
endif()

add_test(
    NAME im.${TEST_MAIN}
    COMMAND ${TEST_MAIN} ${TEST_RUNNER_PARAMS})
