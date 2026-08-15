foreach(_required
    TRACELOOM_ACLGRAPH_ANALYZER
    TRACELOOM_ACLGRAPH_FIXTURE
    TRACELOOM_REMOVED_SIDECAR_PATH)
  if(NOT DEFINED ${_required})
    message(FATAL_ERROR "${_required} is required")
  endif()
endforeach()

file(REMOVE "${TRACELOOM_REMOVED_SIDECAR_PATH}")
execute_process(
  COMMAND "${TRACELOOM_ACLGRAPH_ANALYZER}"
    --fixture "${TRACELOOM_ACLGRAPH_FIXTURE}"
    --compat-sidecar-out "${TRACELOOM_REMOVED_SIDECAR_PATH}"
  RESULT_VARIABLE _result
  OUTPUT_VARIABLE _stdout
  ERROR_VARIABLE _stderr
)

if(_result EQUAL 0)
  message(FATAL_ERROR "removed --compat-sidecar-out option was accepted")
endif()

set(_output "${_stdout}\n${_stderr}")
if(NOT _output MATCHES "unknown argument: --compat-sidecar-out")
  message(FATAL_ERROR
    "removed option failed for the wrong reason: ${_output}")
endif()

if(EXISTS "${TRACELOOM_REMOVED_SIDECAR_PATH}")
  message(FATAL_ERROR "removed option still created a sidecar artifact")
endif()
