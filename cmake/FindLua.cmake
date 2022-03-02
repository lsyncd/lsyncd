# Locate Lua library
# This module defines
#  LUA_EXECUTABLE, if found
#  LUA_FOUND, if false, do not try to link to Lua
#  LUA_LIBRARIES
#  LUA_INCLUDE_DIR, where to find lua.h
#  LUA_VERSION_STRING, the version of Lua found (since CMake 2.8.8)
#
# Note that the expected include convention is
#  #include "lua.h"
# and not
#  #include <lua/lua.h>
# This is because, the lua location is not standardized and may exist
# in locations other than lua/

#=============================================================================
# Copyright 2007-2009 Kitware, Inc.
# Modified to support Lua 5.2 by LuaDist 2012
# Modified to support Lua 5.4 by LuaDist 2022
#
# Distributed under the OSI-approved BSD License (the "License");
# see accompanying file Copyright.txt for details.
#
# This software is distributed WITHOUT ANY WARRANTY; without even the
# implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
# See the License for more information.
#=============================================================================
# (To distribute this file outside of CMake, substitute the full
#  License text for the above reference.)
#
# This module will try to find the newest Lua version down to 5.4

# Always search for non-versioned lua first (recommended)
SET(_POSSIBLE_LUA_INCLUDE include include/lua)
#SET(_POSSIBLE_LUA_EXECUTABLE lua)
#SET(_POSSIBLE_LUA_COMPILER luac)
#SET(_POSSIBLE_LUA_LIBRARY lua)

# Determine possible naming suffixes (there is no standard for this)
SET(_POSSIBLE_SUFFIXES "54" "5.4" "-5.4" "53" "5.3" "-5.3" "52" "5.2" "-5.2" "")

# Set up possible search names and locations
FOREACH(_SUFFIX IN LISTS _POSSIBLE_SUFFIXES)
  LIST(APPEND _POSSIBLE_LUA_INCLUDE "include/lua${_SUFFIX}")
  LIST(APPEND _POSSIBLE_LUA_EXECUTABLE "lua${_SUFFIX}")
  LIST(APPEND _POSSIBLE_LUA_COMPILER "luac${_SUFFIX}")
  LIST(APPEND _POSSIBLE_LUA_LIBRARY "lua${_SUFFIX}")
ENDFOREACH(_SUFFIX)

# Find the lua executable
FIND_PROGRAM(LUA_EXECUTABLE
  NAMES ${_POSSIBLE_LUA_EXECUTABLE}
)

# Find the lua executable
FIND_PROGRAM(LUA_COMPILER
  NAMES luac5.3 ${_POSSIBLE_LUA_COMPILER}
)

# Find the lua header
FIND_PATH(LUA_INCLUDE_DIR lua.h
  HINTS
  $ENV{LUA_DIR}
  PATH_SUFFIXES ${_POSSIBLE_LUA_INCLUDE}
  PATHS
  ~/Library/Frameworks
  /Library/Frameworks
  /usr/local
  /usr
  /sw # Fink
  /opt/local # DarwinPorts
  /opt/csw # Blastwave
  /opt
)

# Find the lua library
FIND_LIBRARY(LUA_LIBRARY
  NAMES ${_POSSIBLE_LUA_LIBRARY}
  HINTS
  $ENV{LUA_DIR}
  PATH_SUFFIXES lib64 lib
  PATHS
  ~/Library/Frameworks
  /Library/Frameworks
  /usr/local
  /usr
  /sw
  /opt/local
  /opt/csw
  /opt
)

IF(LUA_LIBRARY)
  # include the math library for Unix
  IF(UNIX AND NOT APPLE)
    FIND_LIBRARY(LUA_MATH_LIBRARY m)
    SET( LUA_LIBRARIES "${LUA_LIBRARY};${LUA_MATH_LIBRARY}" CACHE STRING "Lua Libraries")
  # For Windows and Mac, don't need to explicitly include the math library
  ELSE(UNIX AND NOT APPLE)
    SET( LUA_LIBRARIES "${LUA_LIBRARY}" CACHE STRING "Lua Libraries")
  ENDIF(UNIX AND NOT APPLE)
ENDIF(LUA_LIBRARY)

# Determine Lua version
IF(LUA_INCLUDE_DIR AND EXISTS "${LUA_INCLUDE_DIR}/lua.h")
  FILE(STRINGS "${LUA_INCLUDE_DIR}/lua.h" lua_version_major_str REGEX "^#define[ \t]+LUA_VERSION_MAJOR[ \t]+\".+\"")
  FILE(STRINGS "${LUA_INCLUDE_DIR}/lua.h" lua_version_minor_str REGEX "^#define[ \t]+LUA_VERSION_MINOR[ \t]+\".+\"")
  FILE(STRINGS "${LUA_INCLUDE_DIR}/lua.h" lua_version_release_str REGEX "^#define[ \t]+LUA_VERSION_RELEASE[ \t]+\".+\"")

  STRING(REGEX REPLACE "^#define[ \t]+LUA_VERSION_MAJOR[ \t]+\"([^\"]+)\".*" "\\1" LUA_VERSION_MAJOR "${lua_version_major_str}")
  STRING(REGEX REPLACE "^#define[ \t]+LUA_VERSION_MINOR[ \t]+\"([^\"]+)\".*" "\\1" LUA_VERSION_MINOR "${lua_version_minor_str}")
  STRING(REGEX REPLACE "^#define[ \t]+LUA_VERSION_RELEASE[ \t]+\"([^\"]+)\".*" "\\1" LUA_VERSION_RELEASE "${lua_version_release_str}")
  
  STRING(CONCAT LUA_VERSION_STRING ${LUA_VERSION_MAJOR} "." ${LUA_VERSION_MINOR} "." ${LUA_VERSION_RELEASE})
  
  UNSET(lua_version_major_str)
  UNSET(lua_version_minor_str)
  UNSET(lua_version_release_str)
ENDIF()

INCLUDE(FindPackageHandleStandardArgs)
# handle the QUIETLY and REQUIRED arguments and set LUA_FOUND to TRUE if
# all listed variables are TRUE
FIND_PACKAGE_HANDLE_STANDARD_ARGS(Lua
                                  REQUIRED_VARS LUA_LIBRARIES LUA_INCLUDE_DIR
                                  VERSION_VAR LUA_VERSION_STRING)

MARK_AS_ADVANCED(LUA_INCLUDE_DIR LUA_LIBRARIES LUA_LIBRARY LUA_MATH_LIBRARY LUA_EXECUTABLE LUA_COMPILER)

