function(PREPEND var prefix)
    set(listVar "")
    foreach(f ${ARGN})
        list(APPEND listVar "${prefix}/${f}")
    endforeach(f)
    set(${var} "${listVar}" PARENT_SCOPE)
endfunction(PREPEND)

macro(INCLUDE_SOURCES OUTPUT DIRECTORY)
    include(${DIRECTORY}/sources.cmake)
    string(TOUPPER ${DIRECTORY} DIRECTORY_UPPER)
    prepend(${DIRECTORY_UPPER}_SOURCES "${DIRECTORY}" ${${DIRECTORY_UPPER}_SOURCES})
    list(APPEND ${OUTPUT} ${${DIRECTORY_UPPER}_SOURCES})
endmacro()
