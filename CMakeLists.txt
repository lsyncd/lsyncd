# preamble
project( Lsyncd )
cmake_minimum_required( VERSION 2.8 )
set( LSYNCD_VERSION 3.0.0-devel )
set( CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/" )


# finding Lua
find_package( Lua REQUIRED )
include_directories ( ${LUA_INCLUDE_DIR} )


# setting Lsyncd sources
set( LSYNCD_SRC core/core.c luacode.c )

# tell systemd via the sd-daemon library about the status of Lsyncd
option( WITH_SYSTEMD "Communicate Lsyncd status to systemd" ON )

# selecting the file notification mechanisms to compile against
option( WITH_INOTIFY "Compile with inotify file notifications (Linux)" ON )

if( WITH_INOTIFY )
	set( LSYNCD_SRC ${LSYNCD_SRC} core/inotify.c )
endif( WITH_INOTIFY )


# generating the config.h file
configure_file (
	"${PROJECT_SOURCE_DIR}/config.h.in"
	"${PROJECT_BINARY_DIR}/config.h"
)
include_directories("${PROJECT_BINARY_DIR}")


# building and compiling the part of lsyncd written in Lua

set( LUA_CODE
	${PROJECT_SOURCE_DIR}/lsyncd.lua
	${PROJECT_SOURCE_DIR}/default/default.lua
	${PROJECT_SOURCE_DIR}/default/rsync.lua
	${PROJECT_SOURCE_DIR}/default/rsyncssh.lua
	${PROJECT_SOURCE_DIR}/default/direct.lua
)

add_custom_command( OUTPUT luacode.c
	COMMAND ${CMAKE_COMMAND} -E echo "Generating luacode linkable"
	COMMAND ${LUA_EXECUTABLE} ${PROJECT_SOURCE_DIR}/bin2carray.lua luacode.out luacode luacode.c
	DEPENDS luacode.out
)

add_custom_command( OUTPUT luacode.out
	COMMAND ${CMAKE_COMMAND} -E echo "Compiling luacode"
	COMMAND ${LUA_COMPILER} -o luacode.out ${LUA_CODE}
	DEPENDS ${LUA_CODE}
)

# the manpage
add_custom_target( manpage
	COMMAND ${CMAKE_COMMAND} -E echo "Updating the manpage"
	COMMAND a2x --format=manpage doc/manpage/lsyncd.1.txt
	DEPENDS doc/manpage/lsyncd.1.txt
)

# the test suite
add_custom_target( tests
	COMMAND echo "Running the tests"
	COMMAND echo "Note you are expected to:"
	COMMAND echo "  * have lua-posix installed"
	COMMAND echo "  * have a passwordless ssh access to localhost"
	COMMAND ${LUA_EXECUTABLE} tests/schedule.lua
	COMMAND ${LUA_EXECUTABLE} tests/l4rsyncdata.lua
	COMMAND ${LUA_EXECUTABLE} tests/filter-rsync.lua
	COMMAND ${LUA_EXECUTABLE} tests/exclude-rsync.lua
	COMMAND ${LUA_EXECUTABLE} tests/exclude-rsyncssh.lua
	COMMAND ${LUA_EXECUTABLE} tests/churn-rsync.lua
	COMMAND ${LUA_EXECUTABLE} tests/churn-rsyncssh.lua
	COMMAND ${LUA_EXECUTABLE} tests/churn-direct.lua
	COMMAND echo "Finished all successfull!"
)

# compiling and linking it all together
add_executable( lsyncd ${LSYNCD_SRC} )
target_link_libraries( lsyncd ${LUA_LIBRARIES} )

install( TARGETS lsyncd RUNTIME DESTINATION bin )
install( FILES doc/manpage/lsyncd.1 DESTINATION man )

