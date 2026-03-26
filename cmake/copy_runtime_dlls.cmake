if(NOT DEFINED dlls OR "${dlls}" STREQUAL "")
    message(STATUS "No runtime DLL dependencies detected for target")
    return()
endif()

if(NOT DEFINED dest OR "${dest}" STREQUAL "")
    message(FATAL_ERROR "Destination directory for runtime DLL copy is not set")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different ${dlls} "${dest}"
    COMMAND_ERROR_IS_FATAL ANY
)
