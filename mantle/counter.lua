--~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
-- counter.lua
--
--
-- Couter tables simply keep a count of the number of elements
-- in them
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


--
-- Metatable.
--
local mt = { }

--
-- Key to native table.
--
local k_nt = { }

--
-- Key to size entry.
--
local k_size = { }

--
-- On accessing a nil index.
--
mt.__index = function
(
	self,
	k   -- key used to access
)
	return self[ k_nt ][ k ]
end

--
-- On assigning a new index.
--
mt.__newindex = function
(
	self,
	k,  -- key value to assign to
	v   -- value to assign
)
	local nt = self[ k_nt ]

	if nt[ k ] == nil
	then
		if v ~= nil then self[ k_size ] = self[ k_size ] + 1 end
	else
		if v == nil then self[ k_size ] = self[ k_size ] - 1 end
	end

	nt[ k ] = v
end

--
-- Returns the length of the counter.
--
mt.__len = function
(
	self
)
	return self[ k_size ]
end

--
-- Allows walking throw the counter.
--
mt.__pairs = function
(
	self
)
	return pairs( self[ k_nt ] )
end


--
-- Allows integral walking throw the counter.
--
mt.__ipairs = function
(
	self
)
	return ipairs( self[ k_nt ] )
end


--
-- Creates a new counter.
--
local function new
( )
	-- k_nt is a native table, private to this object.
	local o =
	{
		[ k_size ] = 0,
		[ k_nt ] = { }
	}

	setmetatable( o, mt )

	return o
end

--
-- Exported interface.
--
Counter = { new = new }

