#include <stdio.h>
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

/* the Lua interpreter */
lua_State* L;

int main (int argc, char *argv[])
{
	/* initialize Lua */
	L = lua_open();
	/* load Lua base libraries */
	luaL_openlibs(L);
	/* register our function */
	//lua_register(L, "average", average);
	/* run the script */
	luaL_dofile(L, "lsyncd.lua");
	/* cleanup Lua */
	lua_close(L);
	/* pause */
	printf( "Press enter to exit..." );
	getchar();
	return 0;
}
