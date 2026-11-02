# SPDX-License-Identifier: GPL-2.0-or-later
if(NOT DEFINED TOOL)
  message(FATAL_ERROR "TOOL must name the source-check executable")
endif()
execute_process(COMMAND "${TOOL}" --version
  RESULT_VARIABLE tool_result OUTPUT_VARIABLE tool_version ERROR_VARIABLE tool_error)
if(NOT tool_result EQUAL 0)
  message(FATAL_ERROR "Cannot query ${TOOL}: ${tool_error}")
endif()
if(NOT tool_version MATCHES "[Vv]ersion 18[.]")
  message(FATAL_ERROR "Source checks require version 18; ${TOOL} reports: ${tool_version}")
endif()
