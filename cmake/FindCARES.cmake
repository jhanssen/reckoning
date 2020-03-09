find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
  pkg_check_modules(PC_CARES QUIET libcares)
endif()

find_path(CARES_INCLUDE_DIR
    NAMES ares.h
    HINTS ${PC_CARES_INCLUDE_DIRS}
    )
mark_as_advanced(CARES_INCLUDE_DIR)

# Look for the library (sorted from most current/relevant entry to least).
find_library(CARES_LIBRARY NAMES
    NAMES cares
    HINTS ${PC_CARES_LIBRARY_DIRS}
    )
mark_as_advanced(CARES_LIBRARY)

if (CARES_INCLUDE_DIR AND CARES_LIBRARY)
    add_library(CARES::libcares UNKNOWN IMPORTED)
    set_target_properties(CARES::libcares PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES ${CARES_INCLUDE_DIR})
    if (EXISTS ${CARES_LIBRARY})
        set_target_properties(CARES::libcares PROPERTIES
            IMPORTED_LINK_INTERFACE_LANGUAGES "C"
            IMPORTED_LOCATION ${CARES_LIBRARY})
    endif()
    message("-- Found C-ARES: ${CARES_LIBRARY}")
else()
    if (CARES_FIND_REQUIRED)
        message(FATAL_ERROR "c-ares not found")
    else()
        message("c-ares not found")
    endif()
endif()
