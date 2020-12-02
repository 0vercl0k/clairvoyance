# Mastho - 2020
link_libraries(kdmp-parser)
find_package (Python3 COMPONENTS Interpreter Development)

# Python on Windows Debug build only load $_d.pyd and require python_d.exe
if(CMAKE_BUILD_TYPE STREQUAL "Debug" AND WIN32)
    set(PYMODULE kdmp_d CACHE INTERNAL "")
else()
    set(PYMODULE kdmp CACHE INTERNAL "")
endif()

# Require Python3 interpreter and development libs
if(NOT Python3_Interpreter_FOUND OR (NOT Python3_Development_FOUND) OR (CMAKE_BUILD_TYPE STREQUAL "Debug" AND WIN32 AND NOT Python3_LIBRARY_DEBUG))
    message(WARNING "Python3 development library not found")
    set(PYMODULE "" CACHE INTERNAL "")
    return()
endif()

Python3_add_library(${PYMODULE} SHARED python-kdmp.cc)

# On Windows Python load $.pyd :: On Linux remove lib$ prefix
if(WIN32)
    set_target_properties(${PYMODULE} PROPERTIES SUFFIX ".pyd")
else()
    set_target_properties(${PYMODULE} PROPERTIES PREFIX "")
endif()

