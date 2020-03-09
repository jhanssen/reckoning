find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
  pkg_check_modules(PC_WEBPDECODER QUIET libwebpdecoder)
endif()

find_path(WEBPDECODER_INCLUDE_DIR
    NAMES webp/decode.h
    HINTS ${PC_WEBPDECODER_INCLUDE_DIRS}
    )
mark_as_advanced(WEBPDECODER_INCLUDE_DIR)

# Look for the library (sorted from most current/relevant entry to least).
find_library(WEBPDECODER_LIBRARY NAMES
    NAMES webpdecoder
    HINTS ${PC_WEBPDECODER_LIBRARY_DIRS}
    )
mark_as_advanced(WEBPDECODER_LIBRARY)

if (WEBPDECODER_INCLUDE_DIR AND WEBPDECODER_LIBRARY)
    add_library(WebP::webpdecoder UNKNOWN IMPORTED)
    set_target_properties(WebP::webpdecoder PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES ${WEBPDECODER_INCLUDE_DIR})
    if (EXISTS ${WEBPDECODER_LIBRARY})
        set_target_properties(WebP::webpdecoder PROPERTIES
            IMPORTED_LINK_INTERFACE_LANGUAGES "C"
            IMPORTED_LOCATION ${WEBPDECODER_LIBRARY})
    endif()
    message("-- Found WebP: ${WEBPDECODER_LIBRARY}")
else()
    if (WEBPDECODER_FIND_REQUIRED)
        message(FATAL_ERROR "WebP decoder not found")
    else()
        message("WebP decoder not found")
    endif()
endif()
