
if(NOT TARGET PNG::PNG)
    add_library(PNG::PNG INTERFACE IMPORTED)

    include("${CONAN_DEPS_DIR}/FindPNG.cmake")

    if(PNG_INCLUDE_DIRS)
        set_target_properties(PNG::PNG PROPERTIES INTERFACE_INCLUDE_DIRECTORIES
            "${PNG_INCLUDE_DIRS}"
        )
    endif()
    string(REPLACE "_static" "" FIXED_PNG_LIBRARY_NAME "${PNG_LIBRARY_LIST}")
    find_library(CONAN_PNG_LIBRARY
        NAMES "${FIXED_PNG_LIBRARY_NAME}"
        PATHS "${PNG_LIB_DIRS}"
    )

    if(NOT CONAN_PNG_LIBRARY)
        message(FATAL_ERROR "Unable to find Conan packaged PNG library")
    endif()

    set_property(TARGET PNG::PNG PROPERTY INTERFACE_LINK_LIBRARIES
                 "${CONAN_PNG_LIBRARY};${PNG_LINKER_FLAGS_LIST}")
    set_property(TARGET PNG::PNG PROPERTY INTERFACE_COMPILE_DEFINITIONS
                 ${PNG_COMPILE_DEFINITIONS})
    set_property(TARGET PNG::PNG PROPERTY INTERFACE_COMPILE_OPTIONS
                 "${PNG_COMPILE_OPTIONS_LIST}")

    # Library dependencies
    include(CMakeFindDependencyMacro)

    if(NOT ZLIB_FOUND)
        find_dependency(ZLIB REQUIRED)
    else()
        message(STATUS "Dependency ZLIB already found")
    endif()
endif()
