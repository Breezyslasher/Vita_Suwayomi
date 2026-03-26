if(NOT DEFINED exe OR "${exe}" STREQUAL "")
    message(FATAL_ERROR "Executable path for runtime dependency scan is not set")
endif()

if(NOT EXISTS "${exe}")
    message(FATAL_ERROR "Executable does not exist: ${exe}")
endif()

if(NOT DEFINED dest OR "${dest}" STREQUAL "")
    message(FATAL_ERROR "Destination directory for runtime DLL copy is not set")
endif()

file(GET_RUNTIME_DEPENDENCIES
    EXECUTABLES "${exe}"
    RESOLVED_DEPENDENCIES_VAR resolved_deps
    UNRESOLVED_DEPENDENCIES_VAR unresolved_deps
    PRE_EXCLUDE_REGEXES "api-ms-win-.*" "ext-ms-.*"
    POST_EXCLUDE_REGEXES ".*/[Ww][Ii][Nn][Dd][Oo][Ww][Ss]/[Ss]ystem32/.*"
    DIRECTORIES "$ENV{PATH}"
)

if(unresolved_deps)
    message(STATUS "Unresolved runtime dependencies: ${unresolved_deps}")
endif()

if(NOT resolved_deps)
    message(STATUS "No runtime DLL dependencies detected for target")
    return()
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different ${resolved_deps} "${dest}"
    COMMAND_ERROR_IS_FATAL ANY
)
