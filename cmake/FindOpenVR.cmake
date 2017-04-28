find_path(
    OPENVR_INCLUDE_DIR
    NAMES
        openvr.h
    HINTS
        ${THIRDPARTY_LIBS_HINTS}/openvr
    PATH_SUFFIXES
        headers
)

find_library(
    OPENVR_LIBRARY
    NAMES
        openvr_api.lib
    HINTS
        ${THIRDPARTY_LIBS_HINTS}/openvr
    PATH_SUFFIXES
        lib/win64
)

find_file(
    OPENVR_DLL
    NAMES
        openvr_api.dll
    HINTS
        ${THIRDPARTY_LIBS_HINTS}/openvr
    PATH_SUFFIXES
        bin/win64
)

message("***** OpenVR Header path:" ${OPENVR_INCLUDE_DIR})
message("***** OpenVR Library path:" ${OPENVR_LIBRARY})
message("***** OpenVR DLL path:" ${OPENVR_DLL})

set(OPENVR_NAMES ${OPENVR_NAMES} OpenVR)

include(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(OpenVR
    DEFAULT_MSG OPENVR_LIBRARY OPENVR_INCLUDE_DIR)

if(OPENVR_FOUND)
    set(OPENVR_LIBRARIES ${OPENVR_LIBRARY})
endif()

mark_as_advanced(
    OPENVR_LIBRARY
    OPENVR_DLL
    OPENVR_INCLUDE_DIR
)
