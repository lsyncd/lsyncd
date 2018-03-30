--
-- monitor.lua from Lsyncd -- the Live (Mirror) Syncing Demon
--
--
-- Holds information about the event monitor capabilities
-- of the core.
--
--
-- After the removal of /dev/events this a mood point since all
-- it can do is only inotify again. But this might improve again.
--
--
-- License: GPLv2 (see COPYING) or any later version
-- Authors: Axel Kittenberger <axkibe@gmail.com>
--
if mantle
then
	print( 'Error, Lsyncd mantle already loaded' )
	os.exit( -1 )
end


-- The cores monitor list
--
local list = { }


--
-- The default event monitor.
--
local function default
( )
	return list[ 1 ]
end


--
-- Initializes with info received from core
--
local function initialize
( clist )
	for k, v in ipairs( clist )
	do
		list[ k ] = v
	end
end


--
-- Exported interface.
--
Monitor =
{
	default = default,
	list = list,
	initialize = initialize
}

