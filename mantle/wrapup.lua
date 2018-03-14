--~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
-- lsyncd.lua   Live (Mirror) Syncing Demon
--
--
-- Wraps up globals of the mantle to set up the Lua
-- space for user scripts.
-- This must come as last mantle file.
--
--
-- License: GPLv2 (see COPYING) or any later version
-- Authors: Axel Kittenberger <axkibe@gmail.com>
--
--~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~


if mantle
then
	print( 'Error, Lsyncd mantle already loaded' )
	os.exit( -1 )
end


-- Lets the core double check version identity with the mantle
mantle = '3.0.0-devel'


Inotify.init( Syncs, uSettings )


core = nil
lockGlobals = nil
Inotify = nil
Delay = nil
InletFactory = nil
Filter = nil
