--~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
-- lsyncd.lua   Live (Mirror) Syncing Demon
--
--
-- Locks globals.
--
-- No more globals can be created after this!
--
--
-- This code assumes your editor is at least 100 chars wide.
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


local t = _G

local mt = getmetatable( t ) or { }


-- TODO try to remove the underscore exceptions
mt.__index = function
(
	t,  -- table being accessed
	k   -- key used to access
)
	if k ~= '_' and string.sub( k, 1, 2 ) ~= '__'
	then
		error( 'Access of non-existing global "' .. k ..'"', 2 )
	else
		rawget( t, k )
	end
end


mt.__newindex = function
(
	t,  -- table getting a new index assigned
	k,  -- key value to assign to
	v   -- value to assign
)
	if k ~= '_' and string.sub( k, 1, 2 ) ~= '__'
	then
		error(
			'Lsyncd does not allow GLOBALS to be created on the fly. '
			.. 'Declare "' .. k.. '" local or declare global on load.',
			2
		)
	else
		rawset( t, k, v )
	end
end


function lockGlobals( )
	setmetatable( t, mt )
end

