# CPU-only exporter and native AugDB contract; no torch, vLLM or accelerator import.
find_package(Python3 3.10 COMPONENTS Interpreter REQUIRED)
add_test(NAME scheduler_context_tests
  COMMAND ${CMAKE_COMMAND} -E env
    TRACELOOM_TEST_BINARY=$<TARGET_FILE:traceloom>
    ${Python3_EXECUTABLE} -m unittest discover -s ${CMAKE_CURRENT_LIST_DIR} -v)
set_tests_properties(scheduler_context_tests PROPERTIES TIMEOUT 120)
