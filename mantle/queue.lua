--
-- queue.lua from Lsyncd -- the Live (Mirror) Syncing Demon
--
--
-- The queue is optimized for FILO operation.
--
--
-- License: GPLv2 (see COPYING) or any later version
-- Authors: Axel Kittenberger <axkibe@gmail.com>
--
if lsyncd_version
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
-- On accessing a nil index.
--
mt.__index = function
(
	self,
	k   -- key used to access
)
	if type( k ) ~= 'number'
	then
		error( 'Queue, key "'..k..'" invalid', 2 )
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
	error( 'Queues are not directly assignable.', 2 )
end

--
-- Returns the size of the queue.
--
mt.__len = function
(
	self
)
	return self[ k_nt ].size
end


--
-- Returns the first item of the Queue.
--
local function first
(
	self
)
	local nt = self[ k_nt ]

	return nt[ nt.first ]
end

--
-- Returns the last item of the Queue.
--
local function last
(
	self
)
	local nt = self[ k_nt ]

	return nt[ nt.last ]
end

--
-- Pushes a value on the queue.
-- Returns the last value
--
local function push
(
	self,
	value   -- value to push
)
	if not value
	then
		error( 'Queue pushing nil value', 2 )
	end

	local nt = self[ k_nt ]

	local last = nt.last + 1

	nt.last = last

	nt[ last ] = value

	nt.size = nt.size + 1

	return last
end


--
-- Removes an item at pos from the Queue.
--
local function remove
(
	self,
	pos   -- position to remove
)
	local nt = self[ k_nt ]

	if nt[ pos ] == nil
	then
		error( 'Removing nonexisting item in Queue', 2 )
	end

	nt[ pos ] = nil

	-- if removing first or last element,
	-- the queue limits are adjusted.
	if pos == nt.first
	then
		local last = nt.last

		while nt[ pos ] == nil and pos <= last
		do
			pos = pos + 1
		end

		nt.first = pos
	elseif pos == nt.last
	then
		local first = nt.first

		while nt[ pos ] == nil and pos >= first
		do
			pos = pos - 1
		end

		nt.last = pos
	end

	-- reset the indizies if the queue is empty
	if nt.last < nt.first
	then
		nt.first = 1

		nt.last = 0
	end

	nt.size = nt.size - 1
end

--
-- Replaces a value.
--
local function replace
(
	self,
	pos,   -- position to replace
	value  -- the new entry
)
	local nt = self[ k_nt ]

	if nt[ pos ] == nil
	then
		error( 'Trying to replace an unset Queue entry.' )
	end

	nt[ pos ] = value
end

--
-- Queue iterator ( stateless )
--
local function iter
(
	self,
	pos
)
	local nt = self[ k_nt ]

	pos = pos + 1

	while nt[ pos ] == nil and pos <= nt.last
	do
		pos = pos + 1
	end

	if pos > nt.last then return nil end

	return pos, nt[ pos ]
end

--
-- Reverse queue iterator (stateless)
--
local function revIter
(
	self,
	pos
)
	local nt = self[ k_nt ]

	pos = pos - 1

	while nt[ pos ] == nil and pos >= nt.first
	do
		pos = pos - 1
	end

	if pos < nt.first
	then
		return nil
	end

	return pos, nt[ pos ]
end


--
-- Iteraters through the queue
-- returning all non-nil pos-value entries.
--
local function qpairs
(
	self
)
	return iter, self, self[ k_nt ].first - 1
end


--
-- Iteraters backwards through the queue
-- returning all non-nil pos-value entries.
--
local function qpairsReverse
(
	self
)
	return revIter, self, self[ k_nt ].last + 1
end

--
-- Creates a new queue.
--
local function new
( )
	local q =
	{
		first = first,
		last = last,
		push = push,
		qpairs = qpairs,
		qpairsReverse = qpairsReverse,
		remove = remove,
		replace = replace,

		[ k_nt ] =
		{
			first = 1,
			last  = 0,
			size  = 0
		}
	}

	setmetatable( q, mt )

	return q
end

--
-- Exported interface
--
Queue = { new = new }

