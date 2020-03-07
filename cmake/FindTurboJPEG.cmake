find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
  pkg_check_modules(PC_TURBOJPEG QUIET libturbojpeg)
endif()

find_path(TURBOJPEG_INCLUDE_DIR
    NAMES turbojpeg.h
    HINTS ${PC_TURBOJPEG_INCLUDE_DIRS}
    )
mark_as_advanced(TURBOJPEG_INCLUDE_DIR)

# Look for the library (sorted from most current/relevant entry to least).
find_library(TURBOJPEG_LIBRARY NAMES
    NAMES turbojpeg
    HINTS ${PC_TURBOJPEG_LIBRARY_DIRS}
    )
mark_as_advanced(TURBOJPEG_LIBRARY)

if (TURBOJPEG_INCLUDE_DIR AND TURBOJPEG_LIBRARY)
    add_library(TurboJPEG::turbojpeg UNKNOWN IMPORTED)
    set_target_properties(TurboJPEG::turbojpeg PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES ${TURBOJPEG_INCLUDE_DIR})
    if (EXISTS ${TURBOJPEG_LIBRARY})
        set_target_properties(TurboJPEG::turbojpeg PROPERTIES
            IMPORTED_LINK_INTERFACE_LANGUAGES "C"
            IMPORTED_LOCATION ${TURBOJPEG_LIBRARY})
    endif()
else()
    if (TURBOJPEG_FIND_REQUIRED)
        message(FATAL_ERROR "TurboJPEG not found")
    else()
        message("TurboJPEG not found")
    endif()
endif()
