# Preamble
project( Lsyncd )
cmake_minimum_required( VERSION 2.8 )
set( LSYNCD_VERSION 3.0.0-devel )
set( CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/" )


# Finding Lua
find_package( Lua REQUIRED )
include_directories ( ${LUA_INCLUDE_DIR} )

# Setting Lsyncd sources.
# Order doesn't matter here.
set( LSYNCD_SRC
	core/log.c
	core/observe.c
	core/pipe.c
	core/main.c
	core/mci.c
	core/mem.c
	core/signal.c
	core/time.c
	core/userobs.c
	core/util.c
	mantle.c
	default.c
)

# Tell systemd via the sd-daemon library about the status of Lsyncd.
option( WITH_SYSTEMD "Communicate Lsyncd status to systemd" ON )

# Selecting the file notification mechanisms to compile against.
option( WITH_INOTIFY "Compile with inotify file notifications (Linux)" ON )

if( WITH_INOTIFY )
	set( LSYNCD_SRC ${LSYNCD_SRC} core/inotify.c )
endif( WITH_INOTIFY )


# Generating the config.h file.
configure_file (
	"${PROJECT_SOURCE_DIR}/config.h.in"
	"${PROJECT_BINARY_DIR}/config.h"
)
include_directories( "${PROJECT_BINARY_DIR}" )


# The mantle written in Lua
#
# Order here matters as the scripts lua will be executed in this order
# on initialization of Lsyncd.
#
set( MANTLE_CODE
	${PROJECT_SOURCE_DIR}/signames.lua
	${PROJECT_SOURCE_DIR}/mantle/array.lua
	${PROJECT_SOURCE_DIR}/mantle/counter.lua
	${PROJECT_SOURCE_DIR}/mantle/queue.lua
	${PROJECT_SOURCE_DIR}/mantle/lock.lua
	${PROJECT_SOURCE_DIR}/mantle/delay.lua
	${PROJECT_SOURCE_DIR}/mantle/inotify.lua
	${PROJECT_SOURCE_DIR}/mantle/combiner.lua
	${PROJECT_SOURCE_DIR}/mantle/inlet.lua
	${PROJECT_SOURCE_DIR}/mantle/filter.lua
	${PROJECT_SOURCE_DIR}/mantle/sync.lua
	${PROJECT_SOURCE_DIR}/mantle/syncmaster.lua
	${PROJECT_SOURCE_DIR}/mantle/monitor.lua
	${PROJECT_SOURCE_DIR}/mantle/fwriter.lua
	${PROJECT_SOURCE_DIR}/mantle/statusfile.lua
	${PROJECT_SOURCE_DIR}/mantle/useralarm.lua
	${PROJECT_SOURCE_DIR}/mantle/mci.lua
	${PROJECT_SOURCE_DIR}/mantle/signal.lua
	${PROJECT_SOURCE_DIR}/mantle/user.lua
	${PROJECT_SOURCE_DIR}/mantle/string.lua
	${PROJECT_SOURCE_DIR}/mantle/version.lua
	${PROJECT_SOURCE_DIR}/mantle/userenv.lua
)

# Generates the signal list for this system
add_custom_command( OUTPUT signames.lua
	COMMAND ${CMAKE_COMMAND} -E echo "Generating signal name list"
	COMMAND ${PROJECT_SOURCE_DIR}/signames.sh signames.lua
	DEPENDS signames.sh
)

# The default sync implementations.
#
# Order here matters as the scripts lua will be executed in this order
# on initialization of Lsyncd.
#
set( DEFAULT_CODE
	${PROJECT_SOURCE_DIR}/default/default.lua
	${PROJECT_SOURCE_DIR}/default/rsync.lua
	${PROJECT_SOURCE_DIR}/default/rsyncssh.lua
	${PROJECT_SOURCE_DIR}/default/direct.lua
)

# Compiling the mantle to Lua bytecode
add_custom_command( OUTPUT mantle.out
	COMMAND ${CMAKE_COMMAND} -E echo "Compiling mantle"
	COMMAND ${LUA_COMPILER} -o mantle.out ${MANTLE_CODE}
	DEPENDS ${MANTLE_CODE}
)

# Compiling the default sync implementations to Lua bytecode
add_custom_command( OUTPUT default.out
	COMMAND ${CMAKE_COMMAND} -E echo "Compiling default"
	COMMAND ${LUA_COMPILER} -o default.out ${DEFAULT_CODE}
	DEPENDS ${DEFAULT_CODE}
)

# Transforms the Lua bytecode into C constants
# It may be possible to use the linker directly to link binary
# Code into the resulting ELF-file, but this proofed to be more
# portable and easier.
add_custom_command( OUTPUT mantle.c
	COMMAND ${CMAKE_COMMAND} -E echo "Generating mantle linkable"
	COMMAND ${LUA_EXECUTABLE} ${PROJECT_SOURCE_DIR}/bin2carray.lua mantle.out mantle mantle.c
	DEPENDS mantle.out
)

add_custom_command( OUTPUT default.c
	COMMAND ${CMAKE_COMMAND} -E echo "Generating default linkable"
	COMMAND ${LUA_EXECUTABLE} ${PROJECT_SOURCE_DIR}/bin2carray.lua default.out default default.c
	DEPENDS default.out
)

# The manpage
add_custom_target( manpage
	COMMAND ${CMAKE_COMMAND} -E echo "Updating the manpage"
	COMMAND a2x --format=manpage doc/manpage/lsyncd.1.txt
	DEPENDS doc/manpage/lsyncd.1.txt
)

# The test suite.
add_custom_target( tests
	COMMAND echo "Running the tests"
	COMMAND echo "Note you are expected to:"
	COMMAND echo "  * have lua-posix installed"
	COMMAND echo "  * have a passwordless ssh access to localhost"
	COMMAND echo "/tmp will be used as testbase"
	COMMAND ${LUA_EXECUTABLE} tests/schedule.lua
	COMMAND ${LUA_EXECUTABLE} tests/l4rsyncdata.lua
	COMMAND ${LUA_EXECUTABLE} tests/filter-rsync.lua
	COMMAND ${LUA_EXECUTABLE} tests/exclude-rsync.lua
	COMMAND ${LUA_EXECUTABLE} tests/exclude-rsyncssh.lua
	COMMAND ${LUA_EXECUTABLE} tests/churn-rsync.lua
	COMMAND ${LUA_EXECUTABLE} tests/churn-rsyncssh.lua
	COMMAND ${LUA_EXECUTABLE} tests/churn-direct.lua
	COMMAND echo "Finished all successfully!"
)

# Compiling and linking it all together.
add_executable( lsyncd ${LSYNCD_SRC} )
target_link_libraries( lsyncd ${LUA_LIBRARIES} )

# Installing.
install( TARGETS lsyncd RUNTIME DESTINATION bin )
install( FILES doc/manpage/lsyncd.1 DESTINATION man )

