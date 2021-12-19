# preamble
project( Lsyncd )
cmake_minimum_required( VERSION 3.10 )
set( LSYNCD_VERSION 2.2.3 )
set( CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/" )


# finding Lua
find_package( Lua REQUIRED )
include_directories ( ${LUA_INCLUDE_DIR} )

# FreeBSD needs this
IF(${CMAKE_SYSTEM_NAME} MATCHES "FreeBSD")
	add_definitions(-D__BSD_VISIBLE)
ENDIF()

# setting Lsyncd sources
set( LSYNCD_SRC lsyncd.c runner.c defaults.c )

include( CheckSymbolExists )
# fork with fd table shared
check_symbol_exists( rfork "unistd.h" HAS_RFORK )

# selecting the file notification mechanisms to compile against
option( WITH_INOTIFY "Compile with inotify file notifications (Linux)" ON )
option( WITH_KQUEUE "Compile with kqueue file notifications (OSX, BSD)" OFF )
option( WITH_FSEVENTS "Compile with /dev/fsevents notifications (OSX)" OFF )

if( WITH_INOTIFY )
	set( LSYNCD_SRC ${LSYNCD_SRC} inotify.c )
endif( WITH_INOTIFY )

if( WITH_KQUEUE )
	set( LSYNCD_SRC ${LSYNCD_SRC} kqueue.c )
endif( WITH_KQUEUE )

if( WITH_FSEVENTS )
	set( LSYNCD_SRC ${LSYNCD_SRC} fsevents.c )

	option( XNU_DIR "Path to the xnu sources" )

#	if( NOT XNU_DIR/bsd/sys/fsevents.h )
#		message( SEND_ERROR "Cannot find bsd/sys/fsevents.h in XNU_DIR" )
#	endif( )

	include_directories( ${XNU_DIR} )
endif( WITH_FSEVENTS )

if ( APPLE )
	set( LSYNCD_TARGET_APPLE 1 )
endif ( APPLE )

# generating the config.h file
configure_file (
	"${PROJECT_SOURCE_DIR}/config.h.in"
	"${PROJECT_BINARY_DIR}/config.h"
)
include_directories("${PROJECT_BINARY_DIR}")


# building and compiling the part of lsyncd written in Lua
# also called "runner"
add_custom_command( OUTPUT runner.c
	COMMAND ${CMAKE_COMMAND} -E echo "Generating built-in runner linkable"
	COMMAND ${LUA_EXECUTABLE} ${PROJECT_SOURCE_DIR}/bin2carray.lua runner.out runner runner.c
	DEPENDS runner.out
)

# this supposes the Lua compiler 'luac' is sitting right next to the Lua interpreter 'lua'
add_custom_command( OUTPUT runner.out
	COMMAND ${CMAKE_COMMAND} -E echo "Compiling built-in runner"
	COMMAND ${LUA_COMPILER} -o runner.out ${PROJECT_SOURCE_DIR}/lsyncd.lua
	DEPENDS ${PROJECT_SOURCE_DIR}/lsyncd.lua
)

# building and compiling the built-in default configs:
#   rsync rysnc-ssh and direct
add_custom_command( OUTPUT defaults.c
	COMMAND ${CMAKE_COMMAND} -E echo "Generating built-in default configs"
	COMMAND ${LUA_EXECUTABLE} ${PROJECT_SOURCE_DIR}/bin2carray.lua defaults.out defaults defaults.c
	DEPENDS defaults.out
)

set( DEFAULT_CONFIGS
	${PROJECT_SOURCE_DIR}/default.lua
	${PROJECT_SOURCE_DIR}/default-rsync.lua
	${PROJECT_SOURCE_DIR}/default-rsyncssh.lua
	${PROJECT_SOURCE_DIR}/default-direct.lua
)

add_custom_command( OUTPUT defaults.out
	COMMAND ${CMAKE_COMMAND} -E echo "Compiling built-in default configs"
	COMMAND ${LUA_COMPILER} -o defaults.out ${DEFAULT_CONFIGS}
	DEPENDS ${DEFAULT_CONFIGS}
)

# the manpage
add_custom_target( manpage
	COMMAND ${CMAKE_COMMAND} -E echo "Updating the manpage"
	COMMAND a2x --format=manpage doc/manpage/lsyncd.1.txt
	DEPENDS doc/manpage/lsyncd.1.txt
)

# create_symlink( ${CMAKE_SOURCE_DIR}/tests tests)
ADD_CUSTOM_TARGET(prepare_tests ALL
                  COMMAND ${CMAKE_COMMAND} -E create_symlink ${CMAKE_SOURCE_DIR}/tests tests
)

add_custom_target( tests
	COMMAND echo "Running the tests"
	COMMAND echo "Note you are expected to:"
	COMMAND echo "  * have lua-posix installed"
	COMMAND ${LUA_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/setup.lua
	COMMAND ${CMAKE_BINARY_DIR}/lsyncd -log all -script ${CMAKE_SOURCE_DIR}/tests/utils_test.lua
	COMMAND ${LUA_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/schedule.lua
	COMMAND ${LUA_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/l4rsyncdata.lua
	COMMAND ${LUA_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/filter-rsync.lua
	COMMAND ${LUA_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/exclude-rsync.lua
	COMMAND ${LUA_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/exclude-rsyncssh.lua
	COMMAND ${LUA_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/churn-rsync.lua
	COMMAND ${LUA_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/churn-rsyncssh.lua
	COMMAND ${LUA_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/churn-direct.lua
	COMMAND ${LUA_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/teardown.lua
	COMMAND echo "Finished all successfull!"
	DEPENDS prepare_tests
)

# compiling and linking it all together
add_executable( lsyncd ${LSYNCD_SRC} )
target_link_libraries( lsyncd ${LUA_LIBRARIES} )

install( TARGETS lsyncd RUNTIME DESTINATION bin )
install( FILES doc/manpage/lsyncd.1 DESTINATION man )
install( DIRECTORY examples DESTINATION doc )

