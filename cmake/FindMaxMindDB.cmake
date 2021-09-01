include(FindPackageHandleStandardArgs)
include(CMakeFindDependencyMacro)

find_dependency(PkgConfig)
pkg_check_modules(PC_MaxMindDB QUIET libmaxminddb)

find_package_handle_standard_args(MaxMindDB
  REQUIRED_VARS
    PC_MaxMindDB_LIBRARIES
  VERSION_VAR
    PC_MaxMindDB_VERSION
)

if(MaxMindDB_FOUND)
  set(MaxMindDB_LIBRARIES ${PC_MaxMindDB_LIBRARIES})
  if(PC_MaxMindDB_INCLUDE_DIRS)
    set(MaxMindDB_INCLUDE_DIRS ${PC_MaxMindDB_INCLUDE_DIRS})
  else()
    unset(MaxMindDB_INCLUDE_DIRS)
  endif()

  if(NOT TARGET MaxMindDB::MaxMindDB)
    add_library(MaxMindDB::MaxMindDB INTERFACE IMPORTED)
    if(MaxMindDB_INCLUDE_DIRS)
        set_property(TARGET MaxMindDB::MaxMindDB PROPERTY
            INTERFACE_INCLUDE_DIRECTORIES "${MaxMindDB_INCLUDE_DIRS}"
        )
    endif()
    set_property(TARGET MaxMindDB::MaxMindDB PROPERTY
        INTERFACE_LINK_LIBRARIES "${MaxMindDB_LIBRARIES}"
    )
  endif()
endif()
