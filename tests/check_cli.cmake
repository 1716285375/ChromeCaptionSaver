execute_process(COMMAND "${PROBE}" ${ARGS}
  RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 5)
if(NOT result EQUAL 1)
  message(FATAL_ERROR "Expected exit 1, got ${result}: ${error}")
endif()
if(NOT error MATCHES "Try --help")
  message(FATAL_ERROR "Expected argument validation diagnostic: ${error}")
endif()
