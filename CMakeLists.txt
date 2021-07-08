# DDNet-based CMakeLists for InfclassR

cmake_minimum_required(VERSION 3.3)

file(STRINGS src/game/version.h VERSION_LINE
  LIMIT_COUNT 1
  REGEX "^#define GAME_RELEASE_VERSION "
)

if(VERSION_LINE MATCHES "\"([0-9]+)\\.([0-9]+)\\.([0-9]+)\"")
  set(VERSION_MAJOR ${CMAKE_MATCH_1})
  set(VERSION_MINOR ${CMAKE_MATCH_2})
  set(VERSION_PATCH ${CMAKE_MATCH_3})
elseif(VERSION_LINE MATCHES "\"([0-9]+)\\.([0-9]+)\"")
  set(VERSION_MAJOR ${CMAKE_MATCH_1})
  set(VERSION_MINOR ${CMAKE_MATCH_2})
  set(VERSION_PATCH "0")
else()
  message(FATAL_ERROR "Couldn't parse version from src/game/version.h")
endif()

if(POLICY CMP0048)
  cmake_policy(SET CMP0048 NEW)
  if(VERSION_PATCH STREQUAL "0")
    project(InfclassR VERSION ${VERSION_MAJOR}.${VERSION_MINOR})
  else()
    project(InfclassR VERSION ${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH})
  endif()
else()
  project(DDNet)
  set(PROJECT_VERSION_MAJOR ${VERSION_MAJOR})
  set(PROJECT_VERSION_MINOR ${VERSION_MINOR})
  set(PROJECT_VERSION_PATCH ${VERSION_PATCH})
  if(VERSION_PATCH STREQUAL "0")
    set(PROJECT_VERSION ${VERSION_MAJOR}.${VERSION_MINOR})
  else()
    set(PROJECT_VERSION ${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH})
  endif()
endif()

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(ORIGINAL_CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH})
set(ORIGINAL_CMAKE_REQUIRED_INCLUDES ${CMAKE_REQUIRED_INCLUDES})
set(ORIGINAL_CMAKE_REQUIRED_LIBRARIES ${CMAKE_REQUIRED_LIBRARIES})
set(OWN_CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${PROJECT_SOURCE_DIR}/cmake)
set(CMAKE_MODULE_PATH ${OWN_CMAKE_MODULE_PATH})

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(TARGET_BITS "64")
elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
  set(TARGET_BITS "32")
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
  set(TARGET_OS "windows")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(TARGET_OS "linux")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
  set(TARGET_OS "mac")
endif()

include(CheckCCompilerFlag)
include(CheckCXXCompilerFlag)
include(CheckSymbolExists)

check_symbol_exists(__i386 "" TARGET_ARCH_X86_i386)
if(TARGET_ARCH_X86_i386)
  set(TARGET_ARCH x86)
else()
  set(TARGET_ARCH)
endif()

set(AUTO_DEPENDENCIES_DEFAULT OFF)
if(TARGET_OS STREQUAL "windows")
  set(AUTO_DEPENDENCIES_DEFAULT ON)
endif()

option(MYSQL "Enable mysql support" OFF)
option(PREFER_BUNDLED_LIBS "Prefer bundled libraries over system libraries" ${AUTO_DEPENDENCIES_DEFAULT})
option(DEV "Don't generate stuff necessary for packaging" OFF)

# Set the default build type to Release
if(NOT(CMAKE_BUILD_TYPE))
  if(NOT(DEV))
    set(CMAKE_BUILD_TYPE Release)
  else()
    set(CMAKE_BUILD_TYPE Debug)
  endif()
endif()

if(NOT(CMAKE_BUILD_TYPE MATCHES "^(Release|Debug|RelWithDebInfo|MinSizeRel)$"))
  message(WARNING "Unknown CMAKE_BUILD_TYPE, should be one of Release, Debug, RelWithDebInfo or MinSizeRel")
endif()

set(DBG $<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>)

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  src/game/version.h
)

set(SERVER_EXECUTABLE Infclass-Server CACHE STRING "Name of the built server executable")

########################################################################
# Compiler flags
########################################################################

function(add_c_compiler_flag_if_supported VARIABLE FLAG)
  if(ARGC GREATER 2)
    set(CHECKED_FLAG "${ARGV2}")
  else()
    set(CHECKED_FLAG "${FLAG}")
  endif()
  string(REGEX REPLACE "[^A-Za-z0-9]" "_" CONFIG_VARIABLE "FLAG_SUPPORTED${CHECKED_FLAG}")
  check_c_compiler_flag("${CHECKED_FLAG}" ${CONFIG_VARIABLE})
  if(${CONFIG_VARIABLE})
    if(${VARIABLE})
      set("${VARIABLE}" "${${VARIABLE}};${FLAG}" PARENT_SCOPE)
    else()
      set("${VARIABLE}" "${FLAG}" PARENT_SCOPE)
    endif()
  endif()
endfunction()

# Force compiler colors on when using ninja. Ninja filters the colors out when
# it's not printing to a terminal on its own.
if(CMAKE_GENERATOR STREQUAL "Ninja")
  add_c_compiler_flag_if_supported(OUR_FLAGS -fdiagnostics-color=always)
  add_c_compiler_flag_if_supported(OUR_FLAGS -fcolor-diagnostics)
endif()

if(NOT MSVC)
  if(CMAKE_VERSION VERSION_LESS 3.1 OR TARGET_OS STREQUAL "mac")
    check_cxx_compiler_flag(-std=gnu++11 FLAG_SUPPORTED_std_gnu__11)
    if(FLAG_SUPPORTED_std_gnu__11)
      if(CMAKE_CXX_FLAGS)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=gnu++11")
      else()
        set(CMAKE_CXX_FLAGS -std=gnu++11)
      endif()
    endif()
  endif()

  # Protect the stack pointer.
  # -fstack-protector-all doesn't work on MinGW.
  add_c_compiler_flag_if_supported(OUR_FLAGS -fstack-protector-all)

  # Inaccurate floating point numbers cause problems on mingw-w64-gcc when
  # compiling for x86, might cause problems elsewhere. So don't store floats
  # in registers but keep them at higher accuracy.

  if(TARGET_ARCH STREQUAL "x86")
    add_c_compiler_flag_if_supported(OUR_FLAGS -ffloat-store)
  endif()

  if(TARGET_OS STREQUAL "mac")
    add_c_compiler_flag_if_supported(OUR_FLAGS -stdlib=libc++)
    add_c_compiler_flag_if_supported(OUR_FLAGS -mmacosx-version-min=10.7)
  endif()

  add_c_compiler_flag_if_supported(OUR_FLAGS_OWN -Wall)
  if(CMAKE_VERSION VERSION_GREATER 3.3 OR CMAKE_VERSION VERSION_EQUAL 3.3)
    add_c_compiler_flag_if_supported(OUR_FLAGS_OWN
      $<$<COMPILE_LANGUAGE:C>:-Wdeclaration-after-statement>
      -Wdeclaration-after-statement
    )
  endif()
  add_c_compiler_flag_if_supported(OUR_FLAGS_OWN -Wextra)
  add_c_compiler_flag_if_supported(OUR_FLAGS_OWN -Wno-unused-parameter)
  add_c_compiler_flag_if_supported(OUR_FLAGS_OWN -Wno-missing-field-initializers)
  add_c_compiler_flag_if_supported(OUR_FLAGS_OWN -Wformat=2) # Warn about format strings.
  add_c_compiler_flag_if_supported(OUR_FLAGS_DEP -Wno-implicit-function-declaration)
  add_c_compiler_flag_if_supported(OUR_FLAGS_OWN -Wduplicated-cond)
  add_c_compiler_flag_if_supported(OUR_FLAGS_OWN -Wduplicated-branches)
  add_c_compiler_flag_if_supported(OUR_FLAGS_OWN -Wlogical-op)
  add_c_compiler_flag_if_supported(OUR_FLAGS_OWN -Wrestrict)
  # TODO: Enable for C++ code except gtest
  #add_cxx_compiler_flag_if_supported(OUR_FLAGS_OWN "-Wuseless-cast")
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Werror=return-type")
endif()

if(NOT MSVC)
  check_c_compiler_flag("-O2;-Wp,-Werror;-D_FORTIFY_SOURCE=2" DEFINE_FORTIFY_SOURCE) # Some distributions define _FORTIFY_SOURCE by themselves.
endif()

########################################################################
# COMMON FUNCTIONS
########################################################################

function(set_glob VAR GLOBBING DIRECTORY) # ...
  file(${GLOBBING} GLOB_RESULT "${DIRECTORY}/*.c" "${DIRECTORY}/*.cpp" "${DIRECTORY}/*.h")
  list(SORT GLOB_RESULT)
  set(FILES)
  foreach(file ${ARGN})
    list(APPEND FILES "${PROJECT_SOURCE_DIR}/${DIRECTORY}/${file}")
  endforeach()

  if(NOT FILES STREQUAL GLOB_RESULT)
    message(AUTHOR_WARNING "${VAR} does not contain every file from directory ${DIRECTORY}")
    set(LIST_BUT_NOT_GLOB)
    if(POLICY CMP0057)
      cmake_policy(SET CMP0057 NEW)
      foreach(file ${FILES})
        if(NOT file IN_LIST GLOB_RESULT)
          list(APPEND LIST_BUT_NOT_GLOB ${file})
        endif()
      endforeach()
      if(LIST_BUT_NOT_GLOB)
        message(AUTHOR_WARNING "Entries only present in ${VAR}: ${LIST_BUT_NOT_GLOB}")
      endif()
      set(GLOB_BUT_NOT_LIST)
      foreach(file ${GLOB_RESULT})
        if(NOT file IN_LIST FILES)
          list(APPEND GLOB_BUT_NOT_LIST ${file})
        endif()
      endforeach()
      if(GLOB_BUT_NOT_LIST)
        message(AUTHOR_WARNING "Entries only present in ${DIRECTORY}: ${GLOB_BUT_NOT_LIST}")
      endif()
      if(NOT LIST_BUT_NOT_GLOB AND NOT GLOB_BUT_NOT_LIST)
        message(AUTHOR_WARNING "${VAR} is not alphabetically sorted")
      endif()
    endif()
  endif()

  set(${VAR} ${FILES} PARENT_SCOPE)
endfunction()

########################################################################
# INITIALIZE TARGET LISTS
########################################################################

set(TARGETS_OWN)
set(TARGETS_DEP)

set(TARGETS_LINK) # Targets with a linking stage.

########################################################################
# DEPENDENCIES
########################################################################

function(set_extra_dirs_lib VARIABLE NAME)
  set("PATHS_${VARIABLE}_LIBDIR" PARENT_SCOPE)
  set("HINTS_${VARIABLE}_LIBDIR" PARENT_SCOPE)
  if(PREFER_BUNDLED_LIBS)
    set(TYPE HINTS)
  else()
    set(TYPE PATHS)
  endif()
  if(TARGET_BITS AND TARGET_OS)
    set(DIR "ddnet-libs/${NAME}/${TARGET_OS}/lib${TARGET_BITS}")
    set("${TYPE}_${VARIABLE}_LIBDIR" "${DIR}" PARENT_SCOPE)
    set("EXTRA_${VARIABLE}_LIBDIR" "${DIR}" PARENT_SCOPE)
  endif()
endfunction()

function(set_extra_dirs_include VARIABLE NAME LIBRARY)
  set("PATHS_${VARIABLE}_INCLUDEDIR" PARENT_SCOPE)
  set("HINTS_${VARIABLE}_INCLUDEDIR" PARENT_SCOPE)
  is_bundled(IS_BUNDLED "${LIBRARY}")
  if(IS_BUNDLED)
    set("HINTS_${VARIABLE}_INCLUDEDIR" "ddnet-libs/${NAME}/include" "ddnet-libs/${NAME}/include/${TARGET_OS}" PARENT_SCOPE)
  endif()
endfunction()

if(CMAKE_CROSSCOMPILING)
  set(CROSSCOMPILING_NO_CMAKE_SYSTEM_PATH NO_CMAKE_SYSTEM_PATH)
else()
  set(CROSSCOMPILING_NO_CMAKE_SYSTEM_PATH)
endif()

function(is_bundled VARIABLE PATH)
  if(PATH)
    string(FIND "${PATH}" "${PROJECT_SOURCE_DIR}" LOCAL_PATH_POS)
    if(LOCAL_PATH_POS EQUAL 0 AND TARGET_BITS AND TARGET_OS)
      set("${VARIABLE}" ON PARENT_SCOPE)
    else()
      set("${VARIABLE}" OFF PARENT_SCOPE)
    endif()
  else()
    set("${VARIABLE}" OFF PARENT_SCOPE)
  endif()
endfunction()

if(NOT CMAKE_CROSSCOMPILING)
  # Check for PkgConfig once so all the other `find_package` calls can do it
  # quietly.
  find_package(PkgConfig)
endif()
find_package(ZLIB)
if(MYSQL)
  find_package(MySQL)
else()
  set(MYSQL_LIBRARIES)
endif()
find_package(PythonInterp)
find_package(Threads)

if(TARGET_OS AND TARGET_OS STREQUAL "mac")
  find_program(DMG dmg)
  find_program(HFSPLUS hfsplus)
  find_program(NEWFS_HFS newfs_hfs)
  if(DMG AND HFSPLUS AND NEWFS_HFS)
    set(DMGTOOLS_FOUND ON)
  else()
    set(DMGTOOLS_FOUND OFF)
  endif()

  find_program(HDIUTIL hdiutil)
endif()

message(STATUS "******** ${CMAKE_PROJECT_NAME} ********")
message(STATUS "Target OS: ${TARGET_OS} ${TARGET_BITS}bit")
message(STATUS "Compiler: ${CMAKE_CXX_COMPILER}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

message(STATUS "Dependencies:")
function(show_dependency_status OUTPUT_NAME NAME)
  if(${NAME}_FOUND)
    if(${NAME}_BUNDLED)
      message(STATUS " * ${OUTPUT_NAME} not found (using bundled version)")
    else()
      message(STATUS " * ${OUTPUT_NAME} found")
    endif()
  else()
    message(STATUS " * ${OUTPUT_NAME} not found")
  endif()
endfunction()

if(TARGET_OS AND TARGET_OS STREQUAL "mac")
  show_dependency_status("Dmg tools" DMGTOOLS)
endif()
if(TARGET_OS AND TARGET_OS STREQUAL "mac")
  show_dependency_status("Hdiutil" HDIUTIL)
endif()
if(MYSQL)
  show_dependency_status("MySQL" MYSQL)
endif()
show_dependency_status("PythonInterp" PYTHONINTERP)
show_dependency_status("Zlib" ZLIB)

if(TARGET_OS STREQUAL "windows")
  set(PLATFORM_LIBS ws2_32) # Windows sockets
elseif(TARGET_OS STREQUAL "mac")
  find_library(CARBON Carbon)
  find_library(COCOA Cocoa)
  find_library(SECURITY Security)
  set(PLATFORM_LIBS ${CARBON} ${SECURITY})
else()
  find_package(X11)
  if(TARGET_OS STREQUAL "linux")
    set(PLATFORM_LIBS rt) # clock_gettime for glibc < 2.17
  else()
    set(PLATFORM_LIBS)
  endif()
endif()

########################################################################
# DEPENDENCY COMPILATION
########################################################################

# Static dependencies
set_glob(DEP_JSON_SRC GLOB src/engine/external/json-parser json.c json.h)
add_library(json EXCLUDE_FROM_ALL OBJECT ${DEP_JSON_SRC})

set_glob(DEP_MD5_SRC GLOB src/engine/external/md5 md5.c md5.h)
add_library(md5 EXCLUDE_FROM_ALL OBJECT ${DEP_MD5_SRC})

list(APPEND TARGETS_DEP json md5)
set(DEP_JSON $<TARGET_OBJECTS:json>)
set(DEP_MD5 $<TARGET_OBJECTS:md5>)

########################################################################
# COPY DATA AND DLLS
########################################################################

file(COPY data DESTINATION .)

########################################################################
# CODE GENERATION
########################################################################

function(generate_source output_file script_parameter)
  add_custom_command(OUTPUT ${output_file}
    COMMAND ${PYTHON_EXECUTABLE} datasrc/compile.py ${script_parameter}
      > "${PROJECT_BINARY_DIR}/${output_file}"
    DEPENDS
      datasrc/compile.py
      datasrc/content.py
      datasrc/datatypes.py
      datasrc/network.py
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
  )
endfunction()

file(MAKE_DIRECTORY "${PROJECT_BINARY_DIR}/src/game/generated/")
execute_process(COMMAND git rev-parse --git-dir
  ERROR_QUIET
  WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
  OUTPUT_VARIABLE PROJECT_GIT_DIR
  OUTPUT_STRIP_TRAILING_WHITESPACE
  RESULT_VARIABLE PROJECT_GIT_DIR_ERROR
)
if(NOT PROJECT_GIT_DIR_ERROR)
  set(GIT_REVISION_EXTRA_DEPS
    ${PROJECT_GIT_DIR}/index
    ${PROJECT_GIT_DIR}/logs/HEAD
  )
endif()
add_custom_command(OUTPUT ${PROJECT_BINARY_DIR}/src/game/generated/git_revision.cpp
  COMMAND ${PYTHON_EXECUTABLE}
    scripts/git_revision.py
    > ${PROJECT_BINARY_DIR}/src/game/generated/git_revision.cpp
  WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
  DEPENDS
    ${GIT_REVISION_EXTRA_DEPS}
    scripts/git_revision.py
)
generate_source("src/game/generated/client_data.cpp" "client_content_source")
generate_source("src/game/generated/client_data.h" "client_content_header")
generate_source("src/game/generated/protocol.cpp" "network_source")
generate_source("src/game/generated/protocol.h" "network_header")
generate_source("src/game/generated/server_data.cpp" "server_content_source")
generate_source("src/game/generated/server_data.h" "server_content_header")


########################################################################
# SHARED
########################################################################

# Sources
set(BASE
  src/base/detect.h
  src/base/math.cpp
  src/base/math.h
  src/base/system.c
  src/base/system.h
  src/base/tl/algorithm.h
  src/base/tl/allocator.h
  src/base/tl/array.h
  src/base/tl/base.h
  src/base/tl/range.h
  src/base/tl/sorted_array.h
  src/base/tl/string.h
  src/base/tl/threading.h
  src/base/vmath.h
)

set(teeuniverses_SOURCES
    src/teeuniverses/components/localization.cpp
    src/teeuniverses/components/localization.h
)

set_glob(ENGINE_INTERFACE GLOB src/engine
  config.h
  console.h
  demo.h
  engine.h
  kernel.h
  map.h
  masterserver.h
  message.h
  server.h
  storage.h
)
set_glob(ENGINE_SHARED GLOB src/engine/shared
  compression.cpp
  compression.h
  config.cpp
  config.h
  config_variables.h
  console.cpp
  console.h
  datafile.cpp
  datafile.h
  demo.cpp
  demo.h
  econ.cpp
  econ.h
  engine.cpp
  filecollection.cpp
  filecollection.h
  huffman.cpp
  huffman.h
  jobs.cpp
  jobs.h
  kernel.cpp
  linereader.cpp
  linereader.h
  map.cpp
  masterserver.cpp
  memheap.cpp
  memheap.h
  message.h
  netban.cpp
  netban.h
  netdatabase.cpp
  netdatabase.h
  network.cpp
  network.h
  network_client.cpp
  network_conn.cpp
  network_console.cpp
  network_console_conn.cpp
  network_server.cpp
  packer.cpp
  packer.h
  protocol.h
  ringbuffer.cpp
  ringbuffer.h
  snapshot.cpp
  snapshot.h
  storage.cpp
)

set_glob(GAME_SHARED GLOB src/game
  animation.cpp
  animation.h
  collision.cpp
  collision.h
  gamecore.cpp
  gamecore.h
  layers.cpp
  layers.h
  mapitems.h
  tuning.h
  variables.h
  version.h
  voting.h
)
# A bit hacky, but these are needed to register all the UUIDs, even for stuff
# that doesn't link game.
set(ENGINE_UUID_SHARED
  src/game/generated/protocol.cpp
  src/game/generated/protocol.h
)
foreach(s ${GAME_SHARED})
  if(s MATCHES "mapitems_(ex.cpp|ex.h|ex_types.h)$")
    list(REMOVE_ITEM GAME_SHARED ${s})
  endif()
endforeach()
list(REMOVE_ITEM GAME_SHARED ${ENGINE_UUID_SHARED})
set(GAME_GENERATED_SHARED
  ${CMAKE_CURRENT_BINARY_DIR}/src/game/generated/git_revision.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/src/game/generated/protocol.h
)

set(DEPS ${DEP_JSON} ${DEP_MD5} ${ZLIB_DEP})

find_package(ICU REQUIRED COMPONENTS uc i18n)

# Libraries
set(LIBS
  ${ZLIB_LIBRARIES}
  ${PLATFORM_LIBS}
  # Add pthreads (on non-Windows) at the end, so that other libraries can depend
  # on it.
  ICU::i18n
  ICU::uc
  ${CMAKE_THREAD_LIBS_INIT}
)

# Targets
add_library(engine-shared EXCLUDE_FROM_ALL OBJECT ${ENGINE_INTERFACE} ${ENGINE_SHARED} ${BASE})
add_library(game-shared EXCLUDE_FROM_ALL OBJECT ${GAME_SHARED} ${GAME_GENERATED_SHARED} ${ENGINE_UUID_SHARED} ${teeuniverses_SOURCES})
list(APPEND TARGETS_OWN engine-shared game-shared)

add_library(pnglite STATIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/external/pnglite/pnglite.c
  ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/external/pnglite/pnglite.h
)
target_include_directories(pnglite PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/external/pnglite
)
add_library(pnglite::pnglite ALIAS pnglite)

include_directories(SYSTEM
	${CMAKE_CURRENT_SOURCE_DIR}/src
	${CMAKE_CURRENT_BINARY_DIR}/src
)

target_link_libraries(engine-shared PUBLIC
  pnglite::pnglite
)

########################################################################
# SERVER
########################################################################

# Sources
set_glob(ENGINE_SERVER GLOB src/engine/server
  crypt.cpp
  crypt.h
  mapconverter.cpp
  mapconverter.h
  #measure_ticks.cpp
  #measure_ticks.h
  netsession.h
  register.cpp
  register.h
  roundstatistics.cpp
  roundstatistics.h
  server.cpp
  server.h
  sql_connector.cpp
  sql_connector.h
  sql_job.cpp
  sql_job.h
  sql_server.cpp
  sql_server.h
  sql_string_helpers.cpp
  sql_string_helpers.h
)

set_glob(GAME_SERVER GLOB_RECURSE src/game/server
  alloc.h
  entities/character.cpp
  entities/character.h
  entities/laser.cpp
  entities/laser.h
  entities/projectile.cpp
  entities/projectile.h
  infclass/classes/humans/human.cpp
  infclass/classes/humans/human.h
  infclass/classes/infcplayerclass.cpp
  infclass/classes/infcplayerclass.h
  infclass/classes/infected/infected.cpp
  infclass/classes/infected/infected.h
  infclass/entities/biologist-laser.cpp
  infclass/entities/biologist-laser.h
  infclass/entities/biologist-mine.cpp
  infclass/entities/biologist-mine.h
  infclass/entities/bouncing-bullet.cpp
  infclass/entities/bouncing-bullet.h
  infclass/entities/engineer-wall.cpp
  infclass/entities/engineer-wall.h
  infclass/entities/flyingpoint.cpp
  infclass/entities/flyingpoint.h
  infclass/entities/growingexplosion.cpp
  infclass/entities/growingexplosion.h
  infclass/entities/hero-flag.cpp
  infclass/entities/hero-flag.h
  infclass/entities/infccharacter.cpp
  infclass/entities/infccharacter.h
  infclass/entities/infcentity.cpp
  infclass/entities/infcentity.h
  infclass/entities/laser-teleport.cpp
  infclass/entities/laser-teleport.h
  infclass/entities/looper-wall.cpp
  infclass/entities/looper-wall.h
  infclass/entities/medic-grenade.cpp
  infclass/entities/medic-grenade.h
  infclass/entities/merc-bomb.cpp
  infclass/entities/merc-bomb.h
  infclass/entities/plasma.cpp
  infclass/entities/plasma.h
  infclass/entities/portal.cpp
  infclass/entities/portal.h
  infclass/entities/scatter-grenade.cpp
  infclass/entities/scatter-grenade.h
  infclass/entities/scientist-laser.cpp
  infclass/entities/scientist-laser.h
  infclass/entities/scientist-mine.cpp
  infclass/entities/scientist-mine.h
  infclass/entities/slug-slime.cpp
  infclass/entities/slug-slime.h
  infclass/entities/soldier-bomb.cpp
  infclass/entities/soldier-bomb.h
  infclass/entities/superweapon-indicator.cpp
  infclass/entities/superweapon-indicator.h
  infclass/entities/turret.cpp
  infclass/entities/turret.h
  infclass/entities/white-hole.cpp
  infclass/entities/white-hole.h
  infclass/infcgamecontroller.cpp
  infclass/infcgamecontroller.h
  infclass/infcplayer.cpp
  infclass/infcplayer.h
  changelog.cpp
  classes.h
  entity.cpp
  entity.h
  eventhandler.cpp
  eventhandler.h
  gamecontext.cpp
  gamecontext.h
  gamecontroller.cpp
  gamecontroller.h
  gameworld.cpp
  gameworld.h
  player.cpp
  player.h
  teeinfo.cpp
  teeinfo.h
)
set(GAME_GENERATED_SERVER
  "src/game/generated/server_data.cpp"
  "src/game/generated/server_data.h"
)
set(SERVER_SRC ${ENGINE_SERVER} ${GAME_SERVER} ${GAME_GENERATED_SERVER})
if(TARGET_OS STREQUAL "windows")
  set(SERVER_ICON "other/icons/DDNet-Server.rc")
else()
  set(SERVER_ICON)
endif()

# Libraries
set(LIBS_SERVER
  ${LIBS}
  ${MYSQL_LIBRARIES}
  # Add pthreads (on non-Windows) at the end, so that other libraries can depend
  # on it.
  ${CMAKE_THREAD_LIBS_INIT}
)

# Target
set(TARGET_SERVER ${SERVER_EXECUTABLE})
add_executable(${TARGET_SERVER}
  ${DEPS}
  ${SERVER_SRC}
  ${SERVER_ICON}
  $<TARGET_OBJECTS:engine-shared>
  $<TARGET_OBJECTS:game-shared>
)
target_link_libraries(${TARGET_SERVER} ${LIBS_SERVER})
list(APPEND TARGETS_OWN ${TARGET_SERVER})
list(APPEND TARGETS_LINK ${TARGET_SERVER})

target_link_libraries(${TARGET_SERVER} md5)
target_link_libraries(${TARGET_SERVER} engine-shared)
target_link_libraries(${TARGET_SERVER} game-shared)
target_link_libraries(${TARGET_SERVER} ICU::i18n)
target_link_libraries(${TARGET_SERVER} ICU::uc)

if(TARGET_OS AND TARGET_OS STREQUAL "mac")
  set(SERVER_LAUNCHER_SRC src/osxlaunch/server.mm)
  set(TARGET_SERVER_LAUNCHER ${TARGET_SERVER}-Launcher)
  add_executable(${TARGET_SERVER_LAUNCHER} ${SERVER_LAUNCHER_SRC})
  target_link_libraries(${TARGET_SERVER_LAUNCHER} ${COCOA})
  list(APPEND TARGETS_OWN ${TARGET_SERVER_LAUNCHER})
  list(APPEND TARGETS_LINK ${TARGET_SERVER_LAUNCHER})
endif()

########################################################################
# VARIOUS TARGETS
########################################################################

set_glob(MASTERSRV_SRC GLOB src/mastersrv mastersrv.cpp mastersrv.h)

set(TARGET_MASTERSRV mastersrv)

add_executable(${TARGET_MASTERSRV} EXCLUDE_FROM_ALL ${MASTERSRV_SRC} $<TARGET_OBJECTS:engine-shared> ${DEPS})
