--~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
-- array.lua
--
--
-- Array tables error if accessed with a non-number.
-- They maintain their length as an attribute and are zero based.
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
	if type( k ) ~= 'number'
	then
		error( 'Array, key "'..k..'" invalid', 2 )
	end

	if k < 0 or k >= self[ k_size ]
	then
		error( 'Array, key "'..k..'" out of bonds', 2 )
	end

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
	if type( k ) ~= 'number'
	then
		error( 'Array, key "'..k..'" invalid', 2 )
	end

	if k < 0 or k > self[ k_size ]
	then
		error( 'Array, key "'..k..'" out of bonds', 2 )
	end

	if k == self[ k_size ]
	then
		self[ k_size ] = self[ k_size ] + 1
	end

	self[ k_nt ][ k ] = v
end

--
-- Returns the length of the array.
--
mt.__len = function
(
	self
)
	return self[ k_size ]
end

--
-- Errors on use of pairs( )
--
mt.__pairs = function
(
	self
)
	error( 'Array, do not use pairs( )', 2 )
end


--
-- Returns next value in iterator.
--
local function iter
(
	self,
	pos
)
	pos = pos + 1

	if pos == self[ k_size ] then return nil end

	return pos, self[ k_nt ][ pos ]
end

--
-- Allows walking throw the array.
--
mt.__ipairs = function
(
	self
)
	return iter, self, -1
end

--
-- Pushes a new new value on the end of the array
--
local function push
(
	self,
	v
)
	self[ k_nt ][ self[ k_size ] ] = v

	self[ k_size ] = self[ k_size ] + 1
end

--
-- Creates a new array.
--
local function new
( )
	-- k_nt is a native table, private to this object.
	local o =
	{
		push = push,
		[ k_size ] = 0,
		[ k_nt ] = { }
	}

	setmetatable( o, mt )

	return o
end

--
-- Exported interface.
--
Array = { new = new }

