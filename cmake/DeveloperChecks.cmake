find_program(CUPID_CLANG_FORMAT NAMES clang-format-18 clang-format)
find_program(CUPID_CLANG_TIDY NAMES clang-tidy-18 clang-tidy)
file(GLOB_RECURSE cupid_check_sources CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.hpp")
if(CUPID_CLANG_FORMAT)
  add_custom_target(format-check
    COMMAND "${CMAKE_COMMAND}" "-DTOOL=${CUPID_CLANG_FORMAT}"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckToolVersion.cmake"
    COMMAND "${CUPID_CLANG_FORMAT}" --dry-run --Werror ${cupid_check_sources}
    COMMENT "Checking C++ source formatting" VERBATIM)
endif()
if(CUPID_CLANG_TIDY AND CMAKE_GENERATOR MATCHES "Ninja|Makefiles")
  add_custom_target(static-check
    COMMAND "${CMAKE_COMMAND}" "-DTOOL=${CUPID_CLANG_TIDY}"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckToolVersion.cmake"
    COMMAND "${CUPID_CLANG_TIDY}" -p "${CMAKE_CURRENT_BINARY_DIR}"
      "-checks=-*,clang-analyzer-core.*,clang-analyzer-cplusplus.NewDelete*,bugprone-infinite-loop,bugprone-sizeof-expression,bugprone-suspicious-memset-usage"
      "-warnings-as-errors=*"
      ${cupid_sources} src/main.cpp
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Checking project sources with Clang static analysis" VERBATIM)
endif()
