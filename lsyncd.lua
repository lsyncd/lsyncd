--~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
-- lsyncd.lua   Live (Mirror) Syncing Demon
--
-- This is the "runner" part of Lsyncd. It containts all its high-level logic.
-- It works closely together with the Lsyncd core in lsyncd.c. This means it
-- cannot be runned directly from the standard lua interpreter.
--
-- This code assumes your editor is at least 100 chars wide.
--
-- License: GPLv2 (see COPYING) or any later version
-- Authors: Axel Kittenberger <axkibe@gmail.com>
--
--~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~


-- require('profiler')
-- profiler.start()


--
-- A security measurement.
-- The core will exit if version ids mismatch.
--
if lsyncd_version
then
	-- ensures the runner is not being loaded twice
	lsyncd.log(
		'Error',
		'You cannot use the lsyncd runner as configuration file!'
	)

	lsyncd.terminate( -1 )
end

lsyncd_version = '2.2.2'


--
-- Hides the core interface from user scripts.
--
local _l = lsyncd
lsyncd = nil

local lsyncd = _l
_l = nil


--
-- Shortcuts (which user is supposed to be able to use them as well)
--
log       = lsyncd.log
terminate = lsyncd.terminate
now       = lsyncd.now
readdir   = lsyncd.readdir


--
-- Coping globals to ensure userscripts cannot change this.
--
local log       = log

local terminate = terminate

local now       = now


--
-- Predeclarations.
--
local Monitors


--
-- Global: total number of processess running.
--
local processCount = 0


--
-- All valid entries in a settings{} call.
--
local settingsCheckgauge =
{
	logfile        = true,
	pidfile        = true,
	nodaemon	   = true,
	statusFile     = true,
	statusInterval = true,
	logfacility    = true,
	logident       = true,
	insist         = true,
	inotifyMode    = true,
	maxProcesses   = true,
	maxDelays      = true,
}


--
-- Settings specified by command line.
--
local clSettings = { }


--
-- Settings specified by config scripts.
--
local uSettings = { }


--
-- A copy of the settings function to see if the
-- user script replaced the settings() by a table
-- ( pre Lsyncd 2.1 style )
--
local settingsSafe


--============================================================================
-- Lsyncd Prototypes
--============================================================================


--
-- Array tables error if accessed with a non-number.
--
local Array = ( function( )

	--
	-- Metatable.
	--
	local mt = { }

	--
	-- On accessing a nil index.
	--
	mt.__index = function
	(
		t,  -- table accessed
		k   -- key value accessed
	)
		if type(k) ~= 'number'
		then
			error( 'Key "'..k..'" invalid for Array', 2 )
		end

		return rawget( t, k )
	end

	--
	-- On assigning a new index.
	--
	mt.__newindex = function
	(
		t,  -- table getting a new index assigned
		k,  -- key value to assign to
		v   -- value to assign
	)
		if type( k ) ~= 'number'
		then
			error( 'Key "'..k..'" invalid for Array', 2 )
		end

		rawset( t, k, v )
	end

	--
	-- Creates a new object
	--
	local function new
	( )
		local o = { }

		setmetatable( o, mt )

		return o
	end

	--
	-- Public interface
	--
	return { new = new }
end )( )


--
-- Count array tables error if accessed with a non-number.
--
-- Additionally they maintain their length as 'size' attribute,
-- since Lua's # operator does not work on tables whose key values are not
-- strictly linear.
--
local CountArray = ( function
( )
	--
	-- Metatable
	--
	local mt = { }

	--
	-- Key to native table
	--
	local k_nt = { }

	--
	-- On accessing a nil index.
	--
	mt.__index = function
	(
		t,  -- table being accessed
		k   -- key used to access
	)
		if type( k ) ~= 'number'
		then
			error( 'Key "' .. k .. '" invalid for CountArray', 2 )
		end

		return t[ k_nt ][ k ]
	end

	--
	-- On assigning a new index.
	--
	mt.__newindex = function
	(
		t,  -- table getting a new index assigned
		k,  -- key value to assign to
		v   -- value to assign
	)
		if type( k ) ~= 'number'
		then
			error( 'Key "'..k..'" invalid for CountArray', 2 )
		end

		-- value before
		local vb = t[ k_nt ][ k ]

		if v and not vb
		then
			t._size = t._size + 1
		elseif not v and vb
		then
			t._size = t._size - 1
		end

		t[ k_nt ][ k ] = v
	end

	--
	-- Walks through all entries in any order.
	--
	local function walk
	(
		self  -- the count array
	)
		return pairs( self[ k_nt ] )
	end

	--
	-- Returns the count.
	--
	local function size
	(
		self  -- the count array
	)
		return self._size
	end

	--
	-- Creates a new count array
	--
	local function new
	( )
		-- k_nt is a native table, private to this object.
		local o =
		{
			_size = 0,
			walk = walk,
			size = size,
			[ k_nt ] = { }
		}

		setmetatable( o, mt )

		return o
	end

	--
	-- Public interface
	--
	return { new = new }
end )( )


--
-- A queue is optimized for pushing and poping.
-- TODO: make this an object
--
Queue = ( function
( )
	--
	-- Metatable
	--
	local mt = { }


	--
	-- Key to native table
	--
	local k_nt = { }


	--
	-- On accessing a nil index.
	--
	mt.__index = function
	(
		t,  -- table being accessed
		k   -- key used to access
	)
		if type( k ) ~= 'number'
		then
			error( 'Key "' .. k .. '" invalid for Queue', 2 )
		end

		return t[ k_nt ][ k ]
	end


	--
	-- On assigning a new index.
	--
	mt.__newindex = function
	(
		t,  -- table getting a new index assigned
		k,  -- key value to assign to
		v   -- value to assign
	)
		error( 'Queues are not directly assignable.', 2 )
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
	-- Returns the size of the queue.
	--
	local function size
	(
		self
	)
		return self[ k_nt ].size
	end


	--
	-- Pushes a value on the queue.
	-- Returns the last value
	--
	local function push
	(
		self,   -- queue to push to
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
		self,  -- the queue
		pos    -- position to remove
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
		self,  -- the queue
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
	-- TODO rename next
	--
	local function iter
	(
		self,  -- queue to iterate
		pos    -- current position
	)
		local nt = self[ k_nt ]

		pos = pos + 1

		while nt[ pos ] == nil and pos <= nt.last
		do
			pos = pos + 1
		end

		if pos > nt.last
		then
			return nil
		end

		return pos, nt[ pos ]
	end


	--
	-- Reverse queue iterator (stateless)
	-- TODO rename prev
	--
	local function iterReverse
	(
		self,  -- queue to iterate
		pos    -- current position
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
		return iterReverse, self, self[ k_nt ].last + 1
	end
	

	--
	-- Creates a new queue.
	--
	local function new
	( )
		local q = {
			first = first,
			last = last,
			push = push,
			qpairs = qpairs,
			qpairsReverse = qpairsReverse,
			remove = remove,
			replace = replace,
			size = size,

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
	-- Public interface
	--
	return { new = new }
end )( )


--
-- Locks globals.
--
-- No more globals can be created after this!
--
local function lockGlobals
( )
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

	setmetatable( t, mt )
end


--
-- Holds the information about a delayed event for one Sync.
--
-- Valid stati of an delay are:
--   'wait'    ... the event is ready to be handled.
--   'active'  ... there is process running catering for this event.
--   'blocked' ... this event waits for another to be handled first.
--
local Delay = ( function
( )
	--
	-- Metatable.
	--
	local mt = { }

	--
	-- Secret key to native table
	--
	local k_nt = { }

	local assignAble =
	{
		dpos   = true,
		etype  = true,
		path   = true,
		path2  = true,
		status = true,
	}

	--
	-- On accessing a nil index.
	--
	mt.__index = function
	(
		t,  -- table accessed
		k   -- key value accessed
	)
		return t[ k_nt ][ k ]
	end

	--
	-- On assigning a new index.
	--
	mt.__newindex = function
	(
		t,  -- table getting a new index assigned
		k,  -- key value to assign to
		v   -- value to assign
	)
		if not assignAble[ k ]
		then
			error( 'Cannot assign new key "' .. k .. '" to Delay' )
		end

		t[ k_nt ][ k ] = v
	end

	--
	-- This delay is being blocked by another delay
	--
	local function blockedBy
	(
		self,  -- this delay
		delay  -- the blocking delay
	)
		self[ k_nt ].status = 'block'

		local blocks = delay[ k_nt ].blocks

		if not blocks
		then
			blocks = { }

			delay[ k_nt ].blocks = blocks
		end

		table.insert( blocks, self )
	end


	--
	-- Sets the delay status to 'active'.
	--
	local function setActive
	(
		self
	)
		self[ k_nt ].status = 'active'
	end

	--
	-- Sets the delay status to 'wait'
	--
	local function wait
	(
		self,   -- this delay
		alarm   -- alarm for the delay
	)
		self[ k_nt ].status = 'wait'

		self[ k_nt ].alarm = alarm
	end

	--
	-- Creates a new delay.
	--
	local function new
	(
		etype,  -- type of event.
		--         'Create', 'Modify', 'Attrib', 'Delete' or 'Move'
		sync,   -- the Sync this delay belongs to
		alarm,  -- latest point in time this should be catered for
		path,   -- path and file-/dirname of the delay relative
		--      -- to the syncs root.
		path2   -- used only in moves, path and file-/dirname of
		        -- move destination
	)
		local delay =
			{
				blockedBy = blockedBy,
				setActive = setActive,
				wait = wait,
				[ k_nt ] =
					{
						etype = etype,
						sync = sync,
						alarm = alarm,
						path = path,
						path2  = path2,
						status = 'wait'
					},
			}

		setmetatable( delay, mt )

		return delay
	end

	--
	-- Public interface
	--
	return { new = new }
end )( )


--
-- Combines delays.
--
local Combiner = ( function
( )
	--
	-- The new delay replaces the old one if it's a file
	--
	local function refi
	(
		d1, -- old delay
		d2  -- new delay
	)
		-- but a directory blocks
		if d2.path:byte( -1 ) == 47
		then
			log(
				'Delay',
				d2.etype,': ',d2.path,
				' blocked by ',
				d1.etype,': ',d1.path
			)

			return 'stack'
		end

		log(
			'Delay',
			d2.etype, ': ', d2.path,
			' replaces ',
			d1.etype, ': ', d1.path
		)

		return 'replace'
	end

	--
	-- Table on how to combine events that dont involve a move.
	--
	local combineNoMove = {

		Attrib = {
			Attrib = 'absorb',
			Modify = 'replace',
			Create = 'replace',
			Delete = 'replace'
		},

		Modify = {
			Attrib = 'absorb',
			Modify = 'absorb',
			Create = 'replace',
			Delete = 'replace'
		},

		Create = {
			Attrib = 'absorb',
			Modify = 'absorb',
			Create = 'absorb',
			Delete = 'replace'
		},

		Delete = {
			Attrib = 'absorb',
			Modify = 'absorb',
			Create = 'replace file,block dir',
			Delete = 'absorb'
		},
	}

	--
	-- Returns the way two Delay should be combined.
	--
	-- Result:
	--    nil               -- They don't affect each other.
	--    'stack'           -- Old Delay blocks new Delay.
	--    'replace'         -- Old Delay is replaced by new Delay.
	--    'absorb'          -- Old Delay absorbs new Delay.
	--    'toDelete,stack'  -- Old Delay is turned into a Delete
	--                         and blocks the new Delay.
	--    'split'           -- New Delay a Move is to be split
	--                         into a Create and Delete.
	--
	local function combine
	(
		d1, -- old delay
		d2  -- new delay
	)
		if d1.etype == 'Init' or d1.etype == 'Blanket'
		then
			return 'stack'
		end

		-- two normal events
		if d1.etype ~= 'Move' and d2.etype ~= 'Move'
		then
			if d1.path == d2.path
			then
				-- lookups up the function in the combination matrix
				-- and calls it
				local result = combineNoMove[ d1.etype ][ d2.etype ]

				if result == 'replace file,block dir'
				then
					if d2.path:byte( -1 ) == 47
					then
						return 'stack'
					else
						return 'replace'
					end
				end
			end

			-- if one is a parent directory of another, events are blocking
			if d1.path:byte( -1 ) == 47 and string.starts( d2.path, d1.path )
			or d2.path:byte( -1 ) == 47 and string.starts( d1.path, d2.path )
			then
				return 'stack'
			end

			return nil
		end

		-- non-move event on a move.
		if d1.etype == 'Move' and d2.etype ~= 'Move'
		then
			-- if the move source could be damaged the events are stacked
			if d1.path == d2.path
			or d2.path:byte( -1 ) == 47 and string.starts( d1.path, d2.path )
			or d1.path:byte( -1 ) == 47 and string.starts( d2.path, d1.path )
			then
				return 'stack'
			end

			--  the event does something with the move destination

			if d1.path2 == d2.path
			then
				if d2.etype == 'Delete'
				or d2.etype == 'Create'
				then
					return 'toDelete,stack'
				end

				-- on 'Attrib' or 'Modify' simply stack on moves
				return 'stack'
			end

			if d2.path:byte( -1 ) == 47 and string.starts( d1.path2, d2.path )
			or d1.path2:byte( -1 ) == 47 and string.starts( d2.path,  d1.path2 )
			then
				return 'stack'
			end

			return nil
		end

		-- a move upon a non-move event
		if d1.etype ~= 'Move' and d2.etype == 'Move'
		then
			if d1.path == d2.path
			or d1.path == d2.path2
			or d1.path:byte( -1 ) == 47 and string.starts( d2.path, d1.path )
			or d1.path:byte( -1 ) == 47 and string.starts( d2.path2, d1.path )
			or d2.path:byte( -1 ) == 47 and string.starts( d1.path, d2.path )
			or d2.path2:byte( -1 ) == 47 and string.starts( d1.path,  d2.path2 )
			then
				return 'split'
			end

			return nil
		end

		--
		-- a move event upon a move event
		--
		if d1.etype == 'Move' and d2.etype == 'Move'
		then
			-- TODO combine moves,
			if d1.path  == d2.path
			or d1.path  == d2.path2
			or d1.path2 == d2.path
			or d2.path2 == d2.path
			or d1.path:byte( -1 ) == 47 and string.starts( d2.path,  d1.path )
			or d1.path:byte( -1 ) == 47 and string.starts( d2.path2, d1.path )
			or d1.path2:byte( -1 ) == 47 and string.starts( d2.path,  d1.path2 )
			or d1.path2:byte( -1 ) == 47 and string.starts( d2.path2, d1.path2 )
			or d2.path:byte( -1 ) == 47 and string.starts( d1.path,  d2.path )
			or d2.path:byte( -1 ) == 47 and string.starts( d1.path2, d2.path )
			or d2.path2:byte( -1 ) == 47 and string.starts( d1.path,  d2.path2 )
			or d2.path2:byte( -1 ) == 47 and string.starts( d1.path2, d2.path2 )
			then
				return 'split'
			end

			return nil
		end

		error( 'reached impossible state' )
	end


	--
	-- The new delay is absorbed by an older one.
	--
	local function logAbsorb
	(
		d1, -- old delay
		d2  -- new delay
	)
		log(
			'Delay',
			d2.etype, ': ',d2.path,
			' absorbed by ',
			d1.etype,': ',d1.path
		)
	end

	--
	-- The new delay replaces the old one if it's a file.
	--
	local function logReplace
	(
		d1, -- old delay
		d2  -- new delay
	)
		log(
			'Delay',
			d2.etype, ': ', d2.path,
			' replaces ',
			d1.etype, ': ', d1.path
		)
	end


	--
	-- The new delay splits on the old one.
	--
	local function logSplit
	(
		d1, -- old delay
		d2  -- new delay
	)
		log(
			'Delay',
			d2.etype, ': ',
			d2.path, ' -> ', d2.path2,
			' splits on ',
			d1.etype, ': ', d1.path
		)
	end

	--
	-- The new delay is blocked by the old delay.
	--
	local function logStack
	(
		d1, -- old delay
		d2  -- new delay
	)
		local active = ''

		if d1.active
		then
			active = 'active '
		end

		if d2.path2
		then
			log(
				'Delay',
				d2.etype, ': ',
				d2.path, '->', d2.path2,
				' blocked by ',
				active,
				d1.etype, ': ', d1.path
			)
		else
			log(
				'Delay',
				d2.etype, ': ', d2.path,
				' blocked by ',
				active,
				d1.etype, ': ', d1.path
			)
		end
	end


	--
	-- The new delay turns the old one (a move) into a delete and is blocked.
	--
	local function logToDeleteStack
	(
		d1, -- old delay
		d2  -- new delay
	)
		if d1.path2
		then
			log(
				'Delay',
				d2.etype, ': ', d2.path,
				' turns ',
			    d1.etype, ': ', d1.path, ' -> ', d1.path2,
				' into Delete: ', d1.path
			)
		else
			log(
				'Delay',
				d2.etype, ': ', d2.path,
				' turns ',
			    d1.etype, ': ', d1.path,
				' into Delete: ', d1.path
			)
		end
	end


	local logFuncs =
	{
		absorb               = logAbsorb,
		replace              = logReplace,
		split                = logSplit,
		stack                = logStack,
		[ 'toDelete,stack' ] = logToDeleteStack
	}


	--
	-- Prints the log message for a combination result
	--
	local function log
	(
		result, -- the combination result
		d1,     -- old delay
		d2      -- new delay
	)
		local lf = logFuncs[ result ]

		if not lf
		then
			error( 'unknown combination result: ' .. result )
		end

		lf( d1, d2 )
	end

	--
	-- Public interface
	--
	return
	{
		combine = combine,
		log = log
	}

end )( )


--
-- Creates inlets for syncs: the user interface for events.
--
local InletFactory = ( function
( )
	--
	-- Table to receive the delay of an event
	-- or the delay list of an event list.
	--
	-- Keys are events and values are delays.
	--
	local e2d = { }

	--
	-- Table to ensure the uniqueness of every event
	-- related to a delay.
	--
	-- Keys are delay and values are events.
	--
	local e2d2 = { }

	--
	-- Allows the garbage collector to remove not refrenced
	-- events.
	--
	setmetatable( e2d,  { __mode = 'k' } )
	setmetatable( e2d2, { __mode = 'v' } )

	--
	-- Removes the trailing slash from a path.
	--
	local function cutSlash
	(
		path -- path to cut
	)
		if string.byte( path, -1 ) == 47
		then
			return string.sub( path, 1, -2 )
		else
			return path
		end
	end

	--
	-- Gets the path of an event.
	--
	local function getPath
	(
		event
	)
		if event.move ~= 'To'
		then
			return e2d[ event ].path
		else
			return e2d[ event ].path2
		end
	end

	--
	-- Interface for user scripts to get event fields.
	--
	local eventFields = {

		--
		-- Returns a copy of the configuration as called by sync.
		-- But including all inherited data and default values.
		--
		-- TODO give user a readonly version.
		--
		config = function
		(
			event
		)
			return e2d[ event ].sync.config
		end,

		--
		-- Returns the inlet belonging to an event.
		--
		inlet = function
		(
			event
		)
			return e2d[ event ].sync.inlet
		end,

		--
		-- Returns the type of the event.
		--
		-- Can be: 'Attrib', 'Create', 'Delete', 'Modify' or 'Move',
		--
		etype = function
		(
			event
		)
			return e2d[ event ].etype
		end,

		--
		-- Events are not lists.
		--
		isList = function
		( )
			return false
		end,

		--
		-- Returns the status of the event.
		--
		-- Can be:
		--    'wait', 'active', 'block'.
		--
		status = function
		(
			event
		)
			return e2d[ event ].status
		end,

		--
		-- Returns true if event relates to a directory
		--
		isdir = function
		(
			event
		)
			return string.byte( getPath( event ), -1 ) == 47
		end,

		--
		-- Returns the name of the file/dir.
		--
		-- Includes a trailing slash for dirs.
		--
		name = function
		(
			event
		)
			return string.match( getPath( event ), '[^/]+/?$' )
		end,

		--
		-- Returns the name of the file/dir
		-- excluding a trailing slash for dirs.
		--
		basename = function
		(
			event
		)
			return string.match( getPath( event ), '([^/]+)/?$')
		end,

		--
		-- Returns the file/dir relative to watch root
		-- including a trailing slash for dirs.
		--
		path = function
		(
			event
		)
			local p = getPath( event )

			if string.byte( p, 1 ) == 47
			then
				p = string.sub( p, 2, -1 )
			end

			return p
		end,

		--
		-- Returns the directory of the file/dir relative to watch root
		-- Always includes a trailing slash.
		--
		pathdir = function
		(
			event
		)
			local p = getPath( event )

			if string.byte( p, 1 ) == 47
			then
				p = string.sub( p, 2, -1 )
			end

			return string.match( p, '^(.*/)[^/]+/?' ) or ''
		end,

		--
		-- Returns the file/dir relativ to watch root
		-- excluding a trailing slash for dirs.
		--
		pathname = function
		(
			event
		)
			local p = getPath( event )

			if string.byte( p, 1 ) == 47
			then
				p = string.sub( p, 2, -1 )
			end

			return cutSlash( p )
		end,

		--
		-- Returns the absolute path of the watch root.
		-- All symlinks are resolved.
		--
		source = function
		(
			event
		)
			return e2d[ event ].sync.source
		end,

		--
		-- Returns the absolute path of the file/dir
		-- including a trailing slash for dirs.
		--
		sourcePath = function
		(
			event
		)
			return e2d[ event ].sync.source .. getPath( event )
		end,

		--
		-- Returns the absolute dir of the file/dir
		-- including a trailing slash.
		--
		sourcePathdir = function
		(
			event
		)
			return(
				e2d[event].sync.source
				.. (
					string.match( getPath( event ), '^(.*/)[^/]+/?' )
					or ''
				)
			)
		end,

		--
		-- Returns the absolute path of the file/dir
		-- excluding a trailing slash for dirs.
		--
		sourcePathname = function
		(
			event
		)
			return e2d[ event ].sync.source .. cutSlash( getPath( event ) )
		end,

		--
		-- Returns the configured target.
		--
		target = function
		(
			event
		)
			return e2d[ event ].sync.config.target
		end,

		--
		-- Returns the relative dir/file appended to the target
		-- including a trailing slash for dirs.
		--
		targetPath = function
		(
			event
		)
			return e2d[ event ].sync.config.target .. getPath( event )
		end,

		--
		-- Returns the dir of the dir/file appended to the target
		-- including a trailing slash.
		--
		targetPathdir = function
		(
			event
		)
			return(
				e2d[ event ].sync.config.target
				.. (
					string.match( getPath( event ), '^(.*/)[^/]+/?' )
					or ''
				)
			)
		end,

		--
		-- Returns the relative dir/file appended to the target
		-- excluding a trailing slash for dirs.
		--
		targetPathname = function( event )
			return(
				e2d[ event ].sync.config.target
				.. cutSlash( getPath( event ) )
			)
		end,
	}

	--
	-- Retrievs event fields for the user script.
	--
	local eventMeta =
	{
		__index = function
		(
			event,
			field
		)
			local f = eventFields[ field ]

			if not f
			then
				if field == 'move'
				then
					-- possibly undefined
					return nil
				end

				error( 'event does not have field "' .. field .. '"', 2 )
			end

			return f( event )
		end
	}

	--
	-- Interface for user scripts to get list fields.
	--
	local eventListFuncs =
	{
		--
		-- Returns a list of paths of all events in list.
		--
		--
		getPaths = function
		(
			elist,   -- handle returned by getevents( )
			mutator  -- if not nil called with ( etype, path, path2 )
			--          returns one or two strings to add.
		)
			local dlist = e2d[ elist ]

			if not dlist
			then
				error( 'cannot find delay list from event list.' )
			end

			local result  = { }
			local resultn = 1

			for k, d in ipairs( dlist )
			do
				local s1, s2

				if mutator
				then
					s1, s2 = mutator( d.etype, d.path, d.path2 )
				else
					s1, s2 = d.path, d.path2
				end

				result[ resultn ] = s1

				resultn = resultn + 1

				if s2
				then
					result[ resultn ] = s2

					resultn = resultn + 1
				end
			end

			return result

		end
	}

	--
	-- Retrievs event list fields for the user script
	--
	local eventListMeta =
	{
		__index = function
		(
			elist,
			func
		)
			if func == 'isList'
			then
				return true
			end

			if func == 'config'
			then
				return e2d[ elist ].sync.config
			end

			local f = eventListFuncs[ func ]

			if not f
			then
				error(
					'event list does not have function "' .. func .. '"',
					2
				)
			end

			return function
			( ... )
				return f( elist, ... )
			end
		end
	}

	--
	-- Table of all inlets with their syncs.
	--
	local inlets = { }

	--
	-- Allows the garbage collector to remove entries.
	--
	setmetatable( inlets, { __mode = 'v' } )

	--
	-- Encapsulates a delay into an event for the user script.
	--
	local function d2e
	(
		delay  -- delay to encapsulate
	)
		-- already created?
		local eu = e2d2[ delay ]

		if delay.etype ~= 'Move'
		then
			if eu
			then
				return eu
			end

			local event = { }

			setmetatable( event, eventMeta )

			e2d[ event ]  = delay

			e2d2[ delay ] = event

			return event
		else
			-- moves have 2 events - origin and destination
			if eu
			then
				return eu[1], eu[2]
			end

			local event  = { move = 'Fr' }
			local event2 = { move = 'To' }

			setmetatable( event, eventMeta )
			setmetatable( event2, eventMeta )

			e2d[ event ]  = delay
			e2d[ event2 ] = delay

			e2d2[ delay ] = { event, event2 }

			-- move events have a field 'move'
			return event, event2
		end
	end

	--
	-- Encapsulates a delay list into an event list for the user script.
	--
	local function dl2el
	(
		dlist
	)
		local eu = e2d2[ dlist ]

		if eu
		then
			return eu
		end

		local elist = { }

		setmetatable( elist, eventListMeta )

		e2d [ elist ] = dlist

		e2d2[ dlist ] = elist

		return elist
	end

	--
	-- The functions the inlet provides.
	--
	local inletFuncs =
	{
		--
		-- Adds an exclude.
		--
		addExclude = function
		(
			sync,   -- the sync of the inlet
			pattern -- exlusion pattern to add
		)
			sync:addExclude( pattern )
		end,

		--
		-- Removes an exclude.
		--
		rmExclude = function
		(
			sync,   -- the sync of the inlet
			pattern -- exlusion pattern to remove
		)
			sync:rmExclude( pattern )
		end,

		--
		-- Gets the list of excludes in their
		-- rsynlike patterns form.
		--
		getExcludes = function
		(
			sync -- the sync of the inlet
		)
			-- creates a copy
			local e = { }
			local en = 1;

			for k, _ in pairs( sync.excludes.list )
			do
				e[ en ] = k;
				en = en + 1;
			end

			return e;
		end,

		--
		-- Creates a blanketEvent that blocks everything
		-- and is blocked by everything.
		--
		createBlanketEvent = function
		(
			sync -- the sync of the inlet
		)
			return d2e( sync:addBlanketDelay( ) )
		end,

		--
		-- Discards a waiting event.
		--
		discardEvent = function
		(
			sync,
			event
		)
			local delay = e2d[ event ]

			if delay.status ~= 'wait'
			then
				log(
					'Error',
					'Ignored cancel of a non-waiting event of type ',
					event.etype
				)

				return
			end

			sync:removeDelay( delay )
		end,

		--
		-- Gets the next not blocked event from queue.
		--
		getEvent = function
		(
			sync
		)
			return d2e( sync:getNextDelay( now( ) ) )
		end,

		--
		-- Gets all events that are not blocked by active events.
		--
		getEvents = function
		(
			sync, -- the sync of the inlet
			test  -- if not nil use this function to test if to include an event
		)
			local dlist = sync:getDelays( test )

			return dl2el( dlist )
		end,

		--
		-- Returns the configuration table specified by sync{ }
		--
		getConfig = function( sync )
			-- TODO give a readonly handler only.
			return sync.config
		end,
	}

	--
	-- Forwards access to inlet functions.
	--
	local inletMeta =
	{
		__index = function
		(
			inlet,
			func
		)
			local f = inletFuncs[ func ]

			if not f
			then
				error(
					'inlet does not have function "'..func..'"',
					2
				)
			end

			return function( ... )
				return f( inlets[ inlet ], ... )
			end
		end,
	}

	--
	-- Creates a new inlet for a sync.
	--
	local function newInlet
	(
		sync  -- the sync to create the inlet for
	)
		-- Lsyncd runner controlled variables
		local inlet = { }

		-- sets use access methods
		setmetatable( inlet, inletMeta )

		inlets[ inlet ] = sync

		return inlet
	end

	--
	-- Returns the delay from a event.
	--
	local function getDelayOrList
	(
		event
	)
		return e2d[ event ]
	end

	--
	-- Returns the sync from an event or list
	--
	local function getSync
	(
		event
	)
		return e2d[ event ].sync
	end

	--
	-- Public interface.
	--
	return {
		getDelayOrList = getDelayOrList,
		d2e            = d2e,
		dl2el          = dl2el,
		getSync        = getSync,
		newInlet       = newInlet,
	}
end )( )


--
-- A set of exclude patterns
--
local Excludes = ( function( )

	--
	-- Turns a rsync like file pattern to a lua pattern.
	-- ( at best it can )
	--
	local function toLuaPattern
	(
		p  --  the rsync like pattern
	)
		local o = p
		p = string.gsub( p, '%%', '%%%%'  )
		p = string.gsub( p, '%^', '%%^'   )
		p = string.gsub( p, '%$', '%%$'   )
		p = string.gsub( p, '%(', '%%('   )
		p = string.gsub( p, '%)', '%%)'   )
		p = string.gsub( p, '%.', '%%.'   )
		p = string.gsub( p, '%[', '%%['   )
		p = string.gsub( p, '%]', '%%]'   )
		p = string.gsub( p, '%+', '%%+'   )
		p = string.gsub( p, '%-', '%%-'   )
		p = string.gsub( p, '%?', '[^/]'  )
		p = string.gsub( p, '%*', '[^/]*' )
		-- this was a ** before
		p = string.gsub( p, '%[%^/%]%*%[%^/%]%*', '.*' )
		p = string.gsub( p, '^/', '^/'    )

		if p:sub( 1, 2 ) ~= '^/'
		then
			-- if does not begin with '^/'
			-- then all matches should begin with '/'.
			p = '/' .. p;
		end

		log(
			'Exclude',
			'toLuaPattern "',
			o, '" = "', p, '"'
		)

		return p
	end

	--
	-- Adds a pattern to exclude.
	--
	local function add
	(
		self,
		pattern  -- the pattern to exclude
	)
		if self.list[ pattern ]
		then -- already in the list
			return
		end

		local lp = toLuaPattern( pattern )

		self.list[ pattern ] = lp
	end

	--
	-- Removes a pattern to exclude.
	--
	local function remove
	(
		self,    -- self
		pattern  -- the pattern to remove
	)

		if not self.list[ pattern ]
		then -- already in the list?
			log(
				'Normal',
				'Removing not excluded exclude "' .. pattern .. '"'
			)

			return
		end

		self.list[ pattern ] = nil
	end

	--
	-- Adds a list of patterns to exclude.
	--
	local function addList
	(
		self,
		plist
	)
		for _, v in ipairs(plist)
		do
			add(self, v)
		end
	end

	--
	-- Loads the excludes from a file.
	--
	local function loadFile
	(
		self,  -- self
		file   -- filename to load from
	)
		f, err = io.open( file )

		if not f
		then
			log(
				'Error',
				'Cannot open exclude file "', file,'": ',
				err
			)

			terminate( -1 )
		end

	    for line in f:lines()
		do
			-- lsyncd 2.0 does not support includes

			if not string.match( line, '^%s*%+' )
			and not string.match( line, '^%s*#' )
			and not string.match( line, '^%s*$' )
			then
				local p = string.match( line, '%s*-?%s*(.*)' )

				if p
				then
					add( self, p )
				end
			end
		end

		f:close( )
	end

	--
	-- Tests if 'path' is excluded.
	--
	local function test
	(
		self,  -- self
		path   -- the path to test
	)
		if path:byte( 1 ) ~= 47
		then
			error( 'Paths for exlusion tests must start with \'/\'' )
		end

		for _, p in pairs( self.list )
		do
			if p:byte( -1 ) == 36
			then
				-- ends with $
				if path:match( p )
				then
					return true
				end
			else
				-- ends either end with / or $
				if path:match( p .. '/' )
				or path:match( p .. '$' )
				then
					return true
				end
			end
		end

		return false
	end

	--
	-- Cretes a new exclude set.
	--
	local function new
	( )
		return {
			list = { },

			-- functions
			add      = add,
			addList  = addList,
			loadFile = loadFile,
			remove   = remove,
			test     = test,
		}
	end

	--
	-- Public interface.
	--
	return { new = new }

end )( )


--
-- Holds information about one observed directory including subdirs.
--
local Sync = ( function
( )
	--
	-- Syncs that have no name specified by the user script
	-- get an incremental default name 'Sync[X]'
	--
	local nextDefaultName = 1

	--
	-- Adds an exclude.
	--
	local function addExclude
	(
		self,
		pattern
	)
		return self.excludes:add( pattern )
	end

	--
	-- Removes an exclude.
	--
	local function rmExclude
	(
		self,
		pattern
	)
		return self.excludes:remove( pattern )
	end

	--
	-- Removes a delay.
	--
	local function removeDelay
	(
		self,
		delay
	)
		if self.delays[ delay.dpos ] ~= delay
		then
			error( 'Queue is broken, delay not at dpos' )
		end

		self.delays:remove( delay.dpos )

		-- frees all delays blocked by this one.
		if delay.blocks
		then
			for _, vd in pairs( delay.blocks )
			do
				vd.status = 'wait'
			end
		end
	end

	--
	-- Returns true if this Sync concerns about 'path'.
	--
	local function concerns
	(
		self,
		path
	)
		-- not concerned if watch rootdir doesn't match
		if not path:starts( self.source )
		then
			return false
		end

		-- a sub dir and not concerned about subdirs
		if self.config.subdirs == false
		and path:sub( #self.source, -1 ):match( '[^/]+/?' )
		then
			return false
		end

		-- concerned if not excluded
		return not self.excludes:test( path:sub( #self.source ) )
	end

	--
	-- Collects a child process.
	--
	local function collect
	(
		self,     -- the sync
		pid,      -- process id of collected child process
		exitcode  -- exitcode of child process
	)
		local delay = self.processes[ pid ]

		if not delay
		then
			-- not a child of this sync.
			return
		end

		if delay.status
		then
			log( 'Delay', 'collected an event' )

			if delay.status ~= 'active'
			then
				error( 'collecting a non-active process' )
			end

			local rc = self.config.collect(
				InletFactory.d2e( delay ),
				exitcode
			)

			if rc == 'die'
			then
				log( 'Error', 'Critical exitcode.' )

				terminate( -1 )
			elseif rc ~= 'again'
			then
				-- if its active again the collecter restarted the event
				removeDelay( self, delay )

				log(
					'Delay',
					'Finish of ',
					delay.etype,
					' on ',
					self.source,delay.path,
					' = ',
					exitcode
				)
			else
				-- sets the delay on wait again
				local alarm = self.config.delay

				-- delays at least 1 second
				if alarm < 1
				then
					alarm = 1
				end

				delay:wait( now( ) + alarm )
			end
		else
			log(
				'Delay',
				'collected a list'
			)

			local rc = self.config.collect(
				InletFactory.dl2el( delay ),
				exitcode
			)

			if rc == 'die'
			then
				log( 'Error', 'Critical exitcode.' );

				terminate( -1 )
			elseif rc == 'again'
			then
				-- sets the delay on wait again
				local alarm = self.config.delay

				-- delays at least 1 second
				if alarm < 1
				then
					alarm = 1
				end

				alarm = now() + alarm

				for _, d in ipairs( delay )
				do
					d:wait( alarm )
				end
			else
				for _, d in ipairs( delay )
				do
					removeDelay( self, d )
				end
			end

			log( 'Delay','Finished list = ',exitcode )
		end

		self.processes[ pid ] = nil
	end

	--
	-- Stacks a newDelay on the oldDelay,
	-- the oldDelay blocks the new Delay.
	--
	-- A delay can block 'n' other delays,
	-- but is blocked at most by one, the latest delay.
	--
	local function stack
	(
		oldDelay,
		newDelay
	)
		newDelay:blockedBy( oldDelay )
	end

	--
	-- Puts an action on the delay stack.
	--
	local function delay
	(
		self,   -- the sync
		etype,  -- the event type
		time,   -- time of the event
		path,   -- path of the event
		path2   -- desitination path of move events
	)
		log(
			'Function',
			'delay( ',
				self.config.name, ', ',
				etype, ', ',
				path, ', ',
				path2,
			' )'
		)

		--
		-- In case new directories were created
		-- looks through this directories and makes create events for
		-- new stuff found in there.
		--
		local function recurse
		( )
			if etype == 'Create' and path:byte( -1 ) == 47
			then
				local entries = lsyncd.readdir( self.source .. path )

				if entries
				then
					for dirname, isdir in pairs( entries )
					do
						local pd = path .. dirname

						if isdir
						then
							pd = pd..'/'
						end

						log(
							'Delay',
							'Create creates Create on ',
							pd
						)

						delay( self, 'Create', time, pd, nil )
					end
				end
			end
		end

		-- exclusion tests
		if not path2
		then
			-- simple test for single path events
			if self.excludes:test( path )
			then
				log(
					'Exclude',
					'excluded ',
					etype,
					' on "',
					path,
					'"'
				)
				return
			end
		else
			-- for double paths ( move ) it might result into a split
			local ex1 = self.excludes:test( path  )

			local ex2 = self.excludes:test( path2 )

			if ex1 and ex2
			then
				log(
					'Exclude',
					'excluded "',
					etype,
					' on "',
					path,
					'" -> "',
					path2,
					'"'
				)

				return
			elseif not ex1 and ex2
			then
				-- splits the move if only partly excluded
				log(
					'Exclude',
					'excluded destination transformed ',
					etype,
					' to Delete ',
					path
				)

				delay(
					self,
					'Delete',
					time,
					path,
					nil
				)

				return
			elseif ex1 and not ex2
			then
				-- splits the move if only partly excluded
				log(
					'Exclude',
					'excluded origin transformed ',
					etype,
					' to Create.',
					path2
				)

				delay(
					self,
					'Create',
					time,
					path2,
					nil
				)

				return
			end
		end

		if etype == 'Move' and not self.config.onMove
		then
			-- if there is no move action defined,
			-- split a move as delete/create
			-- layer 1 scripts which want moves events have to
			-- set onMove simply to 'true'
			log( 'Delay', 'splitting Move into Delete & Create' )

			delay( self, 'Delete', time, path,  nil )

			delay( self, 'Create', time, path2, nil )

			return
		end

		-- creates the new action
		local alarm

		if time and self.config.delay
		then
			alarm = time + self.config.delay
		else
			alarm = now( )
		end

		-- new delay
		local nd = Delay.new(
			etype,
			self,
			alarm,
			path,
			path2
		)

		if nd.etype == 'Init' or nd.etype == 'Blanket'
		then
			-- always stack init or blanket events on the last event
			log(
				'Delay',
				'Stacking ',
				nd.etype,
				' event.'
			)

			if self.delays:size( ) > 0
			then
				stack( self.delays:last( ), nd )
			end

			nd.dpos = self.delays:push( nd )

			recurse( )

			return
		end

		-- detects blocks and combos by working from back until
		-- front through the fifo
		for il, od in self.delays:qpairsReverse( )
		do
			-- asks Combiner what to do
			local ac = Combiner.combine( od, nd )

			if ac
			then
				Combiner.log( ac, od, nd )

				if ac == 'remove'
				then
					self.delays:remove( il )
				elseif ac == 'stack'
				then
					stack( od, nd )

					nd.dpos = self.delays:push( nd )
				elseif ac == 'toDelete,stack'
				then
					if od.status ~= 'active'
					then
						-- turns olddelay into a delete
						local rd = Delay.new( 'Delete', self, od.alarm, od.path )

						self.delays:replace( il, rd )

						rd.dpos = il

						-- and stacks delay2
						stack( rd, nd )
					else
						-- and stacks delay2
						stack( od, nd )
					end

					nd.dpos = self.delays:push( nd )
				elseif ac == 'absorb'
				then
					-- nada
				elseif ac == 'replace'
				then
					if od.status ~= 'active'
					then
						self.delays:replace( il, nd )

						nd.dpos = il
					else
						stack( od, nd )

						nd.dpos = self.delays:push( nd )
					end
				elseif ac == 'split'
				then
					delay( self, 'Delete', time, path,  nil )

					delay( self, 'Create', time, path2, nil )
				else
					error( 'unknown result of combine()' )
				end

				recurse( )

				return
			end

			il = il - 1
		end

		if nd.path2
		then
			log( 'Delay', 'New ', nd.etype, ': ', nd.path, ' -> ', nd.path2 )
		else
			log( 'Delay', 'New ', nd.etype, ': ', nd.path )
		end

		-- no block or combo
		nd.dpos = self.delays:push( nd )

		recurse( )
	end

	--
	-- Returns the soonest alarm for this Sync.
	--
	local function getAlarm
	(
		self
	)
		if self.processes:size( ) >= self.config.maxProcesses
		then
			return false
		end

		-- first checks if more processes could be spawned
		if self.processes:size( ) < self.config.maxProcesses
		then
			-- finds the nearest delay waiting to be spawned
			for _, d in self.delays:qpairs( )
			do
				if d.status == 'wait'
				then
					return d.alarm
				end
			end
		end

		-- nothing to spawn
		return false
	end

	--
	-- Gets all delays that are not blocked by active delays.
	--
	local function getDelays
	(
		self,  -- the sync
		test   -- function to test each delay
	)
		local dlist  = { sync = self }

		local dlistn = 1

		local blocks = { }

		--
		-- inheritly transfers all blocks from delay
		--
		local function getBlocks
		(
			delay
		)
			blocks[ delay ] = true

			if delay.blocks
			then
				for _, d in ipairs( delay.blocks )
				do
					getBlocks( d )
				end
			end
		end

		for _, d in self.delays:qpairs( )
		do
			local tr = true

			if test
			then
				tr = test( InletFactory.d2e( d ) )
			end

			if tr == 'break'
			then
				break
			end

			if d.status == 'active' or not tr
			then
				getBlocks( d )
			elseif not blocks[ d ]
			then
				dlist[ dlistn ] = d

				dlistn = dlistn + 1
			end
		end

		return dlist
	end

	--
	-- Creates new actions
	--
	local function invokeActions
	(
		self,
		timestamp
	)
		log(
			'Function',
			'invokeActions( "',
				self.config.name, '", ',
				timestamp,
			' )'
		)

		if self.processes:size( ) >= self.config.maxProcesses
		then
			-- no new processes
			return
		end

		for _, d in self.delays:qpairs( )
		do
			-- if reached the global limit return
			if uSettings.maxProcesses
			and processCount >= uSettings.maxProcesses
			then
				log('Alarm', 'at global process limit.')

				return
			end

			if self.delays:size( ) < self.config.maxDelays
			then
				-- time constrains are only concerned if not maxed
				-- the delay FIFO already.
				if d.alarm ~= true and timestamp < d.alarm
				then
					-- reached point in stack where delays are in future
					return
				end
			end

			if d.status == 'wait'
			then
				-- found a waiting delay
				if d.etype ~= 'Init'
				then
					self.config.action( self.inlet )
				else
					self.config.init( InletFactory.d2e( d ) )
				end

				if self.processes:size( ) >= self.config.maxProcesses
				then
					-- no further processes
					return
				end
			end
		end
	end

	--
	-- Gets the next event to be processed.
	--
	local function getNextDelay
	(
		self,
		timestamp
	)
		for i, d in self.delays:qpairs( )
		do
			if self.delays:size( ) < self.config.maxDelays
			then
				-- time constrains are only concerned if not maxed
				-- the delay FIFO already.
				if d.alarm ~= true and timestamp < d.alarm
				then
					-- reached point in stack where delays are in future
					return nil
				end
			end

			if d.status == 'wait'
			then
				-- found a waiting delay
				return d
			end
		end

	end

	--
	-- Adds and returns a blanket delay thats blocks all.
	-- Used as custom marker.
	--
	local function addBlanketDelay
	(
		self
	)
		local newd = Delay.new( 'Blanket', self, true, '' )

		newd.dpos = self.delays:push( newd )

		return newd
	end

	--
	-- Adds and returns a blanket delay thats blocks all.
	-- Used as startup marker to call init asap.
	--
	local function addInitDelay
	(
		self
	)
		local newd = Delay.new( 'Init', self, true, '' )

		newd.dpos = self.delays:push( newd )

		return newd
	end

	--
	-- Writes a status report about delays in this sync.
	--
	local function statusReport
	(
		self,
		f
	)
		local spaces = '                    '

		f:write( self.config.name, ' source=', self.source, '\n' )

		f:write( 'There are ', self.delays:size( ), ' delays\n')

		for i, vd in self.delays:qpairs( )
		do
			local st = vd.status

			f:write( st, string.sub( spaces, 1, 7 - #st ) )
			f:write( vd.etype, ' ' )
			f:write( vd.path )

			if vd.path2
			then
				f:write( ' -> ',vd.path2 )
			end

			f:write('\n')

		end

		f:write( 'Excluding:\n' )

		local nothing = true

		for t, p in pairs( self.excludes.list )
		do
			nothing = false
			f:write( t,'\n' )
		end

		if nothing
		then
			f:write('  nothing.\n')
		end

		f:write( '\n' )
	end

	--
	-- Creates a new Sync.
	--
	local function new( config )

		local s =
		{
			-- fields

			config = config,
			delays = Queue.new( ),
			source = config.source,
			processes = CountArray.new( ),
			excludes = Excludes.new( ),

			-- functions

			addBlanketDelay = addBlanketDelay,
			addExclude      = addExclude,
			addInitDelay    = addInitDelay,
			collect         = collect,
			concerns        = concerns,
			delay           = delay,
			getAlarm        = getAlarm,
			getDelays       = getDelays,
			getNextDelay    = getNextDelay,
			invokeActions   = invokeActions,
			removeDelay     = removeDelay,
			rmExclude       = rmExclude,
			statusReport    = statusReport,
		}

		s.inlet = InletFactory.newInlet( s )

		-- provides a default name if needed
		if not config.name
		then
			config.name = 'Sync' .. nextDefaultName
		end

		-- increments defaults if a config name was given or not
		-- so Sync{n} will be the n-th call to sync{}
		nextDefaultName = nextDefaultName + 1

		-- loads exclusions
		if config.exclude
		then
			local te = type( config.exclude )

			if te == 'table'
			then
				s.excludes:addList( config.exclude )
			elseif te == 'string'
			then
				s.excludes:add( config.exclude )
			else
				error( 'type for exclude must be table or string', 2 )
			end

		end

		if
			config.delay ~= nil and
			(
				type( config.delay ) ~= 'number'
				or config.delay < 0
			)
		then
			error( 'delay must be a number and >= 0', 2 )
		end

		if config.excludeFrom
		then
			s.excludes:loadFile( config.excludeFrom )
		end

		return s
	end

	--
	-- Public interface
	--
	return { new = new }

end )( )


--
-- Syncs - a singleton
--
-- Syncs maintains all configured syncs.
--
local Syncs = ( function
( )
	--
	-- the list of all syncs
	--
	local syncsList = Array.new( )

	--
	-- The round robin pointer. In case of global limited maxProcesses
	-- gives every sync equal chances to spawn the next process.
	--
	local round = 1

	--
	-- The cycle( ) sheduler goes into the next round of roundrobin.
	--
	local function nextRound
	( )
		round = round + 1;

		if round > #syncsList
		then
			round = 1
		end

		return round
	end

	--
	-- Returns the round
	--
	local function getRound
	( )
		return round
	end

	--
	-- Returns sync at listpos i
	--
	local function get
	( i )
		return syncsList[ i ];
	end

	--
	-- Helper function for inherit
	-- defined below
	--
	local inheritKV

	--
	-- Recurvely inherits a source table to a destionation table
	-- copying all keys from source.
	--
	-- All entries with integer keys are inherited as additional
	-- sources for non-verbatim tables
	--
	local function inherit
	(
		cd,       -- table copy destination
		cs,       -- table copy source
		verbatim  -- forced verbatim ( for e.g. 'exitcodes' )
	)
		-- First copies all entries with non-integer keys.
		--
		-- Tables are merged; already present keys are not
		-- overwritten
		--
		-- For verbatim tables integer keys are treated like
		-- non-integer keys
		for k, v in pairs( cs )
		do
			if
				(
					type( k ) ~= 'number' or
					verbatim or
					cs._verbatim == true
				)
				and
				(
					type( cs._merge ) ~= 'table' or
					cs._merge[ k ] == true
				)
			then
				inheritKV( cd, k, v )
			end
		end

		-- recursevely inherits all integer keyed tables
		-- ( for non-verbatim tables )
		if cs._verbatim ~= true
		then
			local n = nil

			for k, v in ipairs( cs )
			do
				n = k
				if type( v ) == 'table'
				then
					inherit( cd, v )
				else
					cd[ #cd + 1 ] = v
				end
			end

		end
	end

	--
	-- Helper to inherit. Inherits one key.
	--
	inheritKV =
		function(
			cd,  -- table copy destination
			k,   -- key
			v    -- value
		)

		-- don't merge inheritance controls
		if k == '_merge' or k == '_verbatim'
		then
			return
		end

		local dtype = type( cd [ k ] )

		if type( v ) == 'table'
		then
			if dtype == 'nil'
			then
				cd[ k ] = { }
				inherit( cd[ k ], v, k == 'exitcodes' )
			elseif
				dtype == 'table' and
				v._merge ~= false
			then
				inherit( cd[ k ], v, k == 'exitcodes' )
			end

		elseif dtype == 'nil'
		then
			cd[ k ] = v
		end
	end


	--
	-- Adds a new sync.
	--
	local function add
	(
		config
	)
		-- Checks if user overwrote the settings function.
		-- ( was Lsyncd <2.1 style )
		if settings ~= settingsSafe
		then
			log(
				'Error',
				'Do not use settings = { ... }\n'..
				'      please use settings{ ... } (without the equal sign)'
			)

			os.exit( -1 )
		end

		-- Creates a new config table which inherits all keys/values
		-- from integer keyed tables
		local uconfig = config

		config = { }

		inherit( config, uconfig )

		--
		-- last and least defaults are inherited
		--
		inherit( config, default )

		local inheritSettings = {
			'delay',
			'maxDelays',
			'maxProcesses'
		}

		-- Lets settings override these values.
		for _, v in ipairs( inheritSettings )
		do
			if uSettings[ v ]
			then
				config[ v ] = uSettings[ v ]
			end
		end

		-- Lets commandline override these values.
		for _, v in ipairs( inheritSettings )
		do
			if clSettings[ v ]
			then
				config[ v ] = clSettings[ v ]
			end
		end

		--
		-- lets the userscript 'prepare' function
		-- check and complete the config
		--
		if type( config.prepare ) == 'function'
		then
			-- prepare is given a writeable copy of config
			config.prepare( config, 4 )
		end

		if not config[ 'source' ] then
			local info = debug.getinfo( 3, 'Sl' )
			log(
				'Error',
				info.short_src,':',
				info.currentline,': source missing from sync.'
			)
			terminate( -1 )
		end

		--
		-- absolute path of source
		--
		local realsrc = lsyncd.realdir( config.source )

		if not realsrc
		then
			log(
				'Error',
				'Cannot access source directory: ',
				config.source
			)
			terminate( -1 )
		end

		config._source = config.source
		config.source = realsrc

		if not config.action
		and not config.onAttrib
		and not config.onCreate
		and not config.onModify
		and not config.onDelete
		and not config.onMove
		then
			local info = debug.getinfo( 3, 'Sl' )
			log(
				'Error',
				info.short_src, ':',
				info.currentline,
				': no actions specified.'
			)

			terminate( -1 )
		end

		-- the monitor to use
		config.monitor =
			uSettings.monitor or
			config.monitor or
			Monitors.default( )

		if config.monitor ~= 'inotify'
		and config.monitor ~= 'fsevents'
		then
			local info = debug.getinfo( 3, 'Sl' )

			log(
				'Error',
				info.short_src, ':',
				info.currentline,
				': event monitor "',
				config.monitor,
				'" unknown.'
			)

			terminate( -1 )
		end

		-- creates the new sync
		local s = Sync.new( config )

		table.insert( syncsList, s )

		return s
	end

	--
	-- Allows a for-loop to walk through all syncs.
	--
	local function iwalk
	( )
		return ipairs( syncsList )
	end

	--
	-- Returns the number of syncs.
	--
	local size = function
	( )
		return #syncsList
	end

	--
	-- Tests if any sync is interested in a path.
	--
	local function concerns
	(
		path
	)
		for _, s in ipairs( syncsList )
		do
			if s:concerns( path )
			then
				return true
			end
		end

		return false
	end

	--
	-- Public interface
	--
	return {
		add = add,
		get = get,
		getRound = getRound,
		concerns = concerns,
		iwalk = iwalk,
		nextRound = nextRound,
		size = size
	}
end )( )


--
-- Utility function,
-- Returns the relative part of absolute path if it
-- begins with root
--
local function splitPath
(
	path,
	root
)
	local rlen = #root

	local sp = string.sub( path, 1, rlen )

	if sp == root
	then
		return string.sub( path, rlen, -1 )
	else
		return nil
	end
end

--
-- Interface to inotify.
--
-- watches recursively subdirs and sends events.
--
-- All inotify specific implementation is enclosed here.
--
local Inotify = ( function
( )
	--
	-- A list indexed by inotify watch descriptors yielding
	-- the directories absolute paths.
	--
	local wdpaths = CountArray.new( )

	--
	-- The same vice versa,
	-- all watch descriptors by their absolute paths.
	--
	local pathwds = { }

	--
	-- A list indexed by syncs containing yielding
	-- the root paths the syncs are interested in.
	--
	local syncRoots = { }

	--
	-- Stops watching a directory
	--
	local function removeWatch
	(
		path,  -- absolute path to unwatch
		core   -- if false not actually send the unwatch to the kernel
		--        ( used in moves which reuse the watch )
	)
		local wd = pathwds[ path ]

		if not wd
		then
			return
		end

		if core
		then
			lsyncd.inotify.rmwatch( wd )
		end

		wdpaths[ wd   ] = nil
		pathwds[ path ] = nil
	end


	--
	-- Adds watches for a directory (optionally) including all subdirectories.
	--
	--
	local function addWatch
	(
		path  -- absolute path of directory to observe
	)
		log( 'Function', 'Inotify.addWatch( ', path, ' )' )

		if not Syncs.concerns( path )
		then
			log('Inotify', 'not concerning "', path, '"')

			return
		end

		-- registers the watch
		local inotifyMode = ( uSettings and uSettings.inotifyMode ) or '';

		local wd = lsyncd.inotify.addwatch( path, inotifyMode ) ;

		if wd < 0
		then
			log( 'Inotify','Unable to add watch "', path, '"' )

			return
		end

		do
			-- If this watch descriptor is registered already
			-- the kernel reuses it since the old dir is gone.
			local op = wdpaths[ wd ]

			if op and op ~= path
			then
				pathwds[ op ] = nil
			end
		end

		pathwds[ path ] = wd

		wdpaths[ wd   ] = path

		-- registers and adds watches for all subdirectories
		local entries = lsyncd.readdir( path )

		if not entries
		then
			return
		end

		for dirname, isdir in pairs( entries )
		do
			if isdir
			then
				addWatch( path .. dirname .. '/' )
			end
		end
	end

	--
	-- Adds a Sync to receive events.
	--
	local function addSync
	(
		sync,     -- object to receive events.
		rootdir   -- root dir to watch
	)
		if syncRoots[ sync ]
		then
			error( 'duplicate sync in Inotify.addSync()' )
		end

		syncRoots[ sync ] = rootdir

		addWatch( rootdir )
	end

	--
	-- Called when an event has occured.
	--
	local function event
	(
		etype,     -- 'Attrib', 'Modify', 'Create', 'Delete', 'Move'
		wd,        --  watch descriptor, matches lsyncd.inotifyadd()
		isdir,     --  true if filename is a directory
		time,      --  time of event
		filename,  --  string filename without path
		wd2,       --  watch descriptor for target if it's a Move
		filename2  --  string filename without path of Move target
	)
		if isdir
		then
			filename = filename .. '/'

			if filename2
			then
				filename2 = filename2 .. '/'
			end
		end

		if filename2
		then
			log(
				'Inotify',
				'got event ',
				etype,
				' ',
				filename,
				'(', wd, ') to ',
				filename2,
				'(', wd2 ,')'
			)
		else
			log(
				'Inotify',
				'got event ',
				etype,
				' ',
				filename,
				'(', wd, ')'
			)
		end

		-- looks up the watch descriptor id
		local path = wdpaths[ wd ]

		if path
		then
			path = path..filename
		end

		local path2 = wd2 and wdpaths[ wd2 ]

		if path2 and filename2
		then
			path2 = path2..filename2
		end

		if not path and path2 and etype == 'Move'
		then
			log(
				'Inotify',
				'Move from deleted directory ',
				path2,
				' becomes Create.'
			)

			path  = path2

			path2 = nil

			etype = 'Create'
		end

		if not path
		then
			-- this is normal in case of deleted subdirs
			log(
				'Inotify',
				'event belongs to unknown watch descriptor.'
			)

			return
		end

		for sync, root in pairs( syncRoots )
		do repeat
			local relative  = splitPath( path, root )

			local relative2 = nil

			if path2
			then
				relative2 = splitPath( path2, root )
			end

			if not relative and not relative2
			then
				-- sync is not interested in this dir
				break -- continue
			end

			-- makes a copy of etype to possibly change it
			local etyped = etype

			if etyped == 'Move'
			then
				if not relative2
				then
					log(
						'Normal',
						'Transformed Move to Delete for ',
						sync.config.name
					)

					etyped = 'Delete'
				elseif not relative
				then
					relative = relative2

					relative2 = nil

					log(
						'Normal',
						'Transformed Move to Create for ',
						sync.config.name
					)

					etyped = 'Create'
				end
			end

			if isdir
			then
				if etyped == 'Create'
				then
					addWatch( path )
				elseif etyped == 'Delete'
				then
					removeWatch( path, true )
				elseif etyped == 'Move'
				then
					removeWatch( path, false )
					addWatch( path2 )
				end
			end

			sync:delay( etyped, time, relative, relative2 )

		until true end
	end

	--
	-- Writes a status report about inotify to a file descriptor
	--
	local function statusReport( f )

		f:write( 'Inotify watching ', wdpaths:size(), ' directories\n' )

		for wd, path in wdpaths:walk( )
		do
			f:write( '  ', wd, ': ', path, '\n' )
		end
	end


	--
	-- Public interface.
	--
	return {
		addSync = addSync,
		event = event,
		statusReport = statusReport,
	}

end)( )


--
-- Interface to OSX /dev/fsevents
--
-- This watches all the filesystems at once,
-- but needs root access.
--
-- All fsevents specific implementation are enclosed here.
--
local Fsevents = ( function
( )

	--
	-- A list indexed by syncs yielding
	-- the root path the sync is interested in.
	--
	local syncRoots = { }


	--
	-- Adds a Sync to receive events.
	--
	local function addSync
	(
		sync,  -- object to receive events
		dir    -- dir to watch
	)
		if syncRoots[ sync ]
		then
			error( 'duplicate sync in Fanotify.addSync()' )
		end

		syncRoots[ sync ] = dir

	end

	--
	-- Called when an event has occured.
	--
	local function event
	(
		etype,  --  'Attrib', 'Modify', 'Create', 'Delete', 'Move'
		isdir,  --  true if filename is a directory
		time,   --  time of event
		path,   --  path of file
		path2   --  path of target in case of 'Move'
	)
		if isdir
		then
			path = path .. '/'

			if path2
			then
				path2 = path2 .. '/'
			end
		end

		log(
			'Fsevents',
			etype, ',',
			isdir, ',',
			time,  ',',
			path,  ',',
			path2
		)

		for _, sync in Syncs.iwalk()
		do repeat

			local root = sync.source

			-- TODO combine ifs
			if not path:starts( root )
			then
				if not path2 or not path2:starts( root )
				then
					break  -- continue
				end
			end

			local relative = splitPath( path, root )

			local relative2

			if path2
			then
				relative2 = splitPath( path2, root )
			end

			-- possibly change etype for this iteration only
			local etyped = etype

			if etyped == 'Move'
			then
				if not relative2
				then
					log(
						'Normal',
						'Transformed Move to Delete for ',
						sync.config.name
					)

					etyped = 'Delete'

				elseif not relative
				then
					relative = relative2

					relative2 = nil

					log(
						'Normal',
						'Transformed Move to Create for ',
						sync.config.name
					)

					etyped = 'Create'
				end
			end

			sync:delay( etyped, time, relative, relative2 )

		until true end

	end


	--
	-- Writes a status report about fsevents to a filedescriptor.
	--
	local function statusReport
	(
		f
	)
		-- TODO
	end

	--
	-- Public interface
	--
	return {
		addSync      = addSync,
		event        = event,
		statusReport = statusReport
	}
end )( )


--
-- Holds information about the event monitor capabilities
-- of the core.
--
Monitors = ( function
( )
	--
	-- The cores monitor list
	--
	local list = { }


	--
	-- The default event monitor.
	--
	local function default( )
		return list[ 1 ]
	end


	--
	-- Initializes with info received from core
	--
	local function initialize( clist )
		for k, v in ipairs( clist )
		do
			list[ k ] = v
		end
	end


	--
	-- Public interface
	--
	return {
		default = default,
		list = list,
		initialize = initialize
	}

end)( )

--
-- Writes functions for the user for layer 3 configurations.
--
local functionWriter = ( function( )

	--
	-- All variables known to layer 3 configs.
	--
	transVars = {
		{ '%^pathname',          'event.pathname',        1 },
		{ '%^pathdir',           'event.pathdir',         1 },
		{ '%^path',              'event.path',            1 },
		{ '%^sourcePathname',    'event.sourcePathname',  1 },
		{ '%^sourcePathdir',     'event.sourcePathdir',   1 },
		{ '%^sourcePath',        'event.sourcePath',      1 },
		{ '%^source',            'event.source',          1 },
		{ '%^targetPathname',    'event.targetPathname',  1 },
		{ '%^targetPathdir',     'event.targetPathdir',   1 },
		{ '%^targetPath',        'event.targetPath',      1 },
		{ '%^target',            'event.target',          1 },
		{ '%^o%.pathname',       'event.pathname',        1 },
		{ '%^o%.path',           'event.path',            1 },
		{ '%^o%.sourcePathname', 'event.sourcePathname',  1 },
		{ '%^o%.sourcePathdir',  'event.sourcePathdir',   1 },
		{ '%^o%.sourcePath',     'event.sourcePath',      1 },
		{ '%^o%.targetPathname', 'event.targetPathname',  1 },
		{ '%^o%.targetPathdir',  'event.targetPathdir',   1 },
		{ '%^o%.targetPath',     'event.targetPath',      1 },
		{ '%^d%.pathname',       'event2.pathname',       2 },
		{ '%^d%.path',           'event2.path',           2 },
		{ '%^d%.sourcePathname', 'event2.sourcePathname', 2 },
		{ '%^d%.sourcePathdir',  'event2.sourcePathdir',  2 },
		{ '%^d%.sourcePath',     'event2.sourcePath',     2 },
		{ '%^d%.targetPathname', 'event2.targetPathname', 2 },
		{ '%^d%.targetPathdir',  'event2.targetPathdir',  2 },
		{ '%^d%.targetPath',     'event2.targetPath',     2 },
	}

	--
	-- Splits a user string into its arguments.
	-- Returns a table of arguments
	--
	local function splitStr(
		str -- a string where parameters are seperated by spaces.
	)
		local args = { }

		while str ~= ''
		do
			-- break where argument stops
			local bp = #str

			-- in a quote
			local inQuote = false

			-- tests characters to be space and not within quotes
			for i = 1, #str
			do
				local c = string.sub( str, i, i )

				if c == '"'
				then
					inQuote = not inQuote
				elseif c == ' ' and not inQuote
				then
					bp = i - 1

					break
				end
			end

			local arg = string.sub( str, 1, bp )
			arg = string.gsub( arg, '"', '\\"' )
			table.insert( args, arg )
			str = string.sub( str, bp + 1, -1 )
			str = string.match( str, '^%s*(.-)%s*$' )

		end

		return args
	end


	--
	-- Translates a call to a binary to a lua function.
	-- TODO this has a little too blocking.
	--
	local function translateBinary
	(
		str
	)
		-- splits the string
		local args = splitStr( str )

		-- true if there is a second event
		local haveEvent2 = false

		for ia, iv in ipairs( args ) do

			-- a list of arguments this arg is being split into
			local a = { { true, iv } }

			-- goes through all translates
			for _, v in ipairs( transVars ) do
				local ai = 1
				while ai <= #a do
					if a[ ai ][ 1 ] then
						local pre, post =
							string.match( a[ ai ][ 2 ], '(.*)'..v[1]..'(.*)' )

						if pre then

							if v[3] > 1 then
								haveEvent2 = true
							end

							if pre ~= '' then
								table.insert( a, ai, { true, pre } )
								ai = ai + 1
							end

							a[ ai ] = { false, v[ 2 ] }

							if post ~= '' then
								table.insert( a, ai + 1, { true, post } )
							end
						end
					end
					ai = ai + 1
				end
			end

			-- concats the argument pieces into a string.
			local as = ''
			local first = true

			for _, v in ipairs( a ) do

				if not first then
					as = as..' .. '
				end

				if v[ 1 ] then
					as = as .. '"' .. v[ 2 ] .. '"'
				else
					as = as .. v[ 2 ]
				end

				first = false
			end

			args[ ia ] = as
		end

		local ft
		if not haveEvent2
		then
			ft = 'function( event )\n'
		else
			ft = 'function( event, event2 )\n'
		end

		ft = ft ..
			"    log('Normal', 'Event ', event.etype, \n" ..
			"        ' spawns action \"".. str.."\"')\n" ..
			"    spawn( event"

		for _, v in ipairs( args )
		do
			ft = ft .. ',\n         ' .. v
		end

		ft = ft .. ')\nend'
		return ft

	end


	--
	-- Translates a call using a shell to a lua function
	--
	local function translateShell
	(
		str
	)
		local argn = 1

		local args = { }

		local cmd = str

		local lc = str

		-- true if there is a second event
		local haveEvent2 = false

		for _, v in ipairs( transVars )
		do
			local occur = false

			cmd = string.gsub(
				cmd,
				v[ 1 ],
				function( )
					occur = true
					return '"$' .. argn .. '"'
				end
			)

			lc = string.gsub(
				lc,
				v[1],
				']]..' .. v[2] .. '..[['
			)

			if occur
			then
				argn = argn + 1

				table.insert( args, v[ 2 ] )

				if v[ 3 ] > 1
				then
					haveEvent2 = true
				end
			end

		end

		local ft

		if not haveEvent2
		then
			ft = 'function( event )\n'
		else
			ft = 'function( event, event2 )\n'
		end

		-- TODO do array joining instead
		ft = ft..
			"    log('Normal', 'Event ',event.etype,\n"..
			"        [[ spawns shell \""..lc.."\"]])\n"..
			"    spawnShell(event, [["..cmd.."]]"

		for _, v in ipairs( args )
		do
			ft = ft..',\n         '..v
		end

		ft = ft .. ')\nend'

		return ft

	end

	--
	-- Writes a lua function for a layer 3 user script.
	--
	local function translate
	(
		str
	)
		-- trims spaces
		str = string.match( str, '^%s*(.-)%s*$' )

		local ft
		if string.byte( str, 1, 1 ) == 47
		then
			-- starts with /
			 ft = translateBinary( str )
		elseif string.byte( str, 1, 1 ) == 94
		then
			-- starts with ^
			 ft = translateShell( str:sub( 2, -1 ) )
		else
			 ft = translateShell( str )
		end

		log(
			'FWrite',
			'translated "',
			str,
			'" to \n',
			ft
		)

		return ft
	end


	--
	-- Public interface.
	--
	return { translate = translate }

end )( )



--
-- Writes a status report file at most every 'statusintervall' seconds.
--
local StatusFile = ( function
( )


	--
	-- Timestamp when the status file has been written.
	--
	local lastWritten = false


	--
	-- Timestamp when a status file should be written.
	--
	local alarm = false


	--
	-- Returns the alarm when the status file should be written-
	--
	local function getAlarm
	( )
		return alarm
	end


	--
	-- Called to check if to write a status file.
	--
	local function write
	(
		timestamp
	)
		log( 'Function', 'write( ', timestamp, ' )' )

		--
		-- takes care not write too often
		--
		if uSettings.statusInterval > 0
		then
			-- already waiting?
			if alarm and timestamp < alarm
			then
				log(
					'Statusfile',
					'waiting(',
					timestamp,
					' < ',
					alarm,
					')'
				)

				return
			end

			-- determines when a next write will be possible
			if not alarm
			then
				local nextWrite =
					lastWritten and timestamp
					+ uSettings.statusInterval

				if nextWrite and timestamp < nextWrite
				then
					log(
						'Statusfile',
						'setting alarm: ',
						nextWrite
					)
					alarm = nextWrite

					return
				end
			end

			lastWritten = timestamp
			alarm = false
		end

		log( 'Statusfile', 'writing now' )

		local f, err = io.open( uSettings.statusFile, 'w' )

		if not f
		then
			log(
				'Error',
				'Cannot open status file "' ..
					uSettings.statusFile ..
					'" :' ..
					err
			)
			return
		end

		f:write( 'Lsyncd status report at ', os.date( ), '\n\n' )

		for i, s in Syncs.iwalk( )
		do
			s:statusReport( f )

			f:write( '\n' )
		end

		Inotify.statusReport( f )

		f:close( )
	end


	--
	-- Public interface
	--
	return {
		write = write,
		getAlarm = getAlarm
	}

end )( )


--
-- Lets userscripts make their own alarms.
--
local UserAlarms = ( function
( )
	local alarms = { }

	--
	-- Calls the user function at timestamp.
	--
	local function alarm
	(
		timestamp,
		func,
		extra
	)
		local idx

		for k, v in ipairs( alarms )
		do
			if timestamp < v.timestamp
			then
				idx = k

				break
			end
		end

		local a = {
			timestamp = timestamp,
			func = func,
			extra = extra
		}

		if idx
		then
			table.insert( alarms, idx, a )
		else
			table.insert( alarms, a )
		end
	end


	--
	-- Retrieves the soonest alarm.
	--
	local function getAlarm
	( )
		if #alarms == 0
		then
			return false
		else
			return alarms[1].timestamp
		end
	end


	--
	-- Calls user alarms.
	--
	local function invoke( timestamp )
		while
			#alarms > 0 and
			alarms[ 1 ].timestamp <= timestamp
		do
			alarms[ 1 ].func( alarms[ 1 ].timestamp, alarms[ 1 ].extra )
			table.remove( alarms, 1 )
		end
	end


	--
	-- Public interface
	--
	return {
		alarm    = alarm,
		getAlarm = getAlarm,
		invoke   = invoke
	}

end )( )

--============================================================================
-- Lsyncd runner's plugs. These functions are called from core.
--============================================================================

--
-- Current status of Lsyncd.
--
-- 'init'  ... on (re)init
-- 'run'   ... normal operation
-- 'fade'  ... waits for remaining processes
--
local lsyncdStatus = 'init'

--
-- The cores interface to the runner.
--
local runner = { }

--
-- Last time said to be waiting for more child processes
--
local lastReportedWaiting = false

--
-- Called from core whenever Lua code failed.
--
-- Logs a backtrace
--
function runner.callError
(
	message
)
	log('Error', 'in Lua: ', message )

	-- prints backtrace
	local level = 2

	while true
	do
		local info = debug.getinfo( level, 'Sl' )

		if not info
		then
			terminate( -1 )
		end

		log(
			'Error',
			'Backtrace ',
			level - 1, ' :',
			info.short_src, ':',
			info.currentline
		)

		level = level + 1
	end
end


--
-- Called from core whenever a child process has finished and
-- the zombie process was collected by core.
--
function runner.collectProcess( pid, exitcode )

	processCount = processCount - 1

	if processCount < 0 then
		error( 'negative number of processes!' )
	end

	for _, s in Syncs.iwalk() do
		if s:collect(pid, exitcode) then return end
	end

end

--
-- Called from core everytime a masterloop cycle runs through.
--
-- This happens in case of
--   * an expired alarm.
--   * a returned child process.
--   * received filesystem events.
--   * received a HUP, TERM or INT signal.
--
function runner.cycle(
	timestamp   -- the current kernel time (in jiffies)
)
	log( 'Function', 'cycle( ', timestamp, ' )' )

	if lsyncdStatus == 'fade'
	then
		if processCount > 0
		then
			if
				lastReportedWaiting == false or
				timestamp >= lastReportedWaiting + 60
			then
				lastReportedWaiting = timestamp

				log(
					'Normal',
					'waiting for ',
					processCount,
					' more child processes.'
				)
			end

			return true
		else
			return false
		end
	end

	if lsyncdStatus ~= 'run'
	then
		error( 'runner.cycle() called while not running!' )
	end

	--
	-- goes through all syncs and spawns more actions
	-- if possibly. But only let Syncs invoke actions if
	-- not at global limit
	--
	if not uSettings.maxProcesses
	or processCount < uSettings.maxProcesses
	then
		local start = Syncs.getRound( )

		local ir = start

		repeat
			local s = Syncs.get( ir )

			s:invokeActions( timestamp )

			ir = ir + 1

			if ir > Syncs.size( )
			then
				ir = 1
			end
		until ir == start

		Syncs.nextRound( )
	end

	UserAlarms.invoke( timestamp )

	if uSettings.statusFile
	then
		StatusFile.write( timestamp )
	end

	return true
end

--
-- Called by core if '-help' or '--help' is in
-- the arguments.
--
function runner.help( )
	io.stdout:write(
[[

USAGE:
  runs a config file:
    lsyncd [OPTIONS] [CONFIG-FILE]

  default rsync behaviour:
    lsyncd [OPTIONS] -rsync [SOURCE] [TARGET]

  default rsync with mv's through ssh:
    lsyncd [OPTIONS] -rsyncssh [SOURCE] [HOST] [TARGETDIR]

  default local copying mechanisms (cp|mv|rm):
    lsyncd [OPTIONS] -direct [SOURCE] [TARGETDIR]

OPTIONS:
  -delay SECS         Overrides default delay times
  -help               Shows this
  -insist             Continues startup even if it cannot connect
  -log    all         Logs everything (debug)
  -log    scarce      Logs errors only
  -log    [Category]  Turns on logging for a debug category
  -logfile FILE       Writes log to FILE (DEFAULT: uses syslog)
  -nodaemon           Does not detach and logs to stdout/stderr
  -pidfile FILE       Writes Lsyncds PID into FILE
  -runner FILE        Loads Lsyncds lua part from FILE
  -version            Prints versions and exits

LICENSE:
  GPLv2 or any later version.

SEE:
  `man lsyncd` for further information.

]])

--
--  -monitor NAME       Uses operating systems event montior NAME
--                      (inotify/fanotify/fsevents)

	os.exit( -1 )
end


--
-- Called from core to parse the command line arguments
--
-- returns a string as user script to load.
--    or simply 'true' if running with rsync bevaiour
--
-- terminates on invalid arguments.
--
function runner.configure( args, monitors )

	Monitors.initialize( monitors )

	--
	-- a list of all valid options
	--
	-- first paramter is the number of parameters an option takes
	-- if < 0 the called function has to check the presence of
	-- optional arguments.
	--
	-- second paramter is the function to call
	--
	local options = {

		-- log is handled by core already.

		delay =
			{
				1,
				function( secs )
					clSettings.delay = secs + 0
				end
			},

		insist =
			{
				0,
				function( )
					clSettings.insist = true
				end
			},

		log =
			{
				1,
				nil
			},

		logfile =
			{
				1,
				function( file )
					clSettings.logfile = file
				end
			},

		monitor =
			{
				-1,
				function( monitor )
					if not monitor then
						io.stdout:write( 'This Lsyncd supports these monitors:\n' )
						for _, v in ipairs(Monitors.list) do
							io.stdout:write('   ',v,'\n')
						end

						io.stdout:write('\n')

						lsyncd.terminate(-1)
					else
						clSettings.monitor = monitor
					end
				end
			},

		nodaemon =
			{
				0,
				function( )
					clSettings.nodaemon = true
				end
			},

		pidfile =
			{
				1,
				function( file )
					clSettings.pidfile=file
				end
			},

		rsync    =
			{
				2,
				function( src, trg )
					clSettings.syncs = clSettings.syncs or { }
					table.insert(
						clSettings.syncs,
						{ 'rsync', src, trg }
					)
				end
			},

		rsyncssh =
			{
				3,
				function( src, host, tdir )
					clSettings.syncs = clSettings.syncs or { }
					table.insert(
						clSettings.syncs,
						{ 'rsyncssh', src, host, tdir }
					)
				end
			},

		direct =
			{
				2,
				function( src, trg )
					clSettings.syncs = clSettings.syncs or { }
					table.insert(
						clSettings.syncs,
						{ 'direct', src, trg }
					)
				end
			},

		version =
			{
				0,
				function( )
					io.stdout:write( 'Version: ', lsyncd_version, '\n' )
					os.exit( 0 )
				end
			}
	}

	-- non-opts is filled with all args that were no part dash options

	local nonopts = { }

	local i = 1
	while i <= #args do

		local a = args[ i ]

		if a:sub( 1, 1 ) ~= '-' then
			table.insert( nonopts, args[ i ] )
		else
			if a:sub( 1, 2 ) == '--' then
				a = a:sub( 3 )
			else
				a = a:sub( 2 )
			end

			local o = options[ a ]

			if not o
			then
				log(
					'Error',
					'unknown option command line option ',
					args[i]
				)
				os.exit( -1 )
			end

			if o[ 1 ] >= 0 and i + o[ 1 ] > #args
			then
				log( 'Error', a ,' needs ', o[ 1 ],' arguments' )

				os.exit( -1 )
			elseif o[1] < 0
			then
				o[ 1 ] = -o[ 1 ]
			end

			if o[ 2 ]
			then
				if o[ 1 ] == 0
				then
					o[ 2 ]( )
				elseif o[ 1 ] == 1
				then
					o[ 2 ]( args[ i + 1] )
				elseif o[ 1 ] == 2
				then
					o[ 2 ]( args[ i + 1], args[ i + 2] )
				elseif o[ 1 ] == 3
				then
					o[ 2 ]( args[ i + 1], args[ i + 2], args[ i + 3] )
				end
			end

			i = i + o[1]
		end

		i = i + 1
	end

	if clSettings.syncs
	then
		if #nonopts ~= 0
		then
			log(
				'Error',
				'There cannot be command line syncs and a config file together.'
			)
			os.exit( -1 )
		end

	else

		if #nonopts == 0
		then
			runner.help( args[ 0 ] )
		elseif #nonopts == 1
		then
			return nonopts[ 1 ]
		else
			-- TODO make this possible
			log(
				'Error',
				'There can only be one config file in the command line.'
			)

			os.exit( -1 )
		end

	end
end


--
-- Called from core on init or restart after user configuration.
--
-- firstTime:
--    true when Lsyncd startups the first time,
--    false on resets, due to HUP signal or monitor queue overflow.
--
function runner.initialize( firstTime )

	-- Checks if user overwrote the settings function.
	-- ( was Lsyncd <2.1 style )
	if settings ~= settingsSafe
	then
		log(
			'Error',
			'Do not use settings = { ... }\n'..
			'      please use settings{ ... } (without the equal sign)'
		)

		os.exit( -1 )
	end

	lastReportedWaiting = false

	--
	-- From this point on, no globals may be created anymore
	--
	lockGlobals( )

	--
	-- copies simple settings with numeric keys to 'key = true' settings.
	--
	-- FIXME this can be removed when
	-- Lsyncd 2.0.x backwards compatibility is dropped
	--
--	for k, v in ipairs( uSettings )
--	do
--		if uSettings[ v ]
--		then
--			log(
--				'Error',
--				'Double setting "' .. v.. '"'
--			)
--			os.exit( -1 )
--		end
--
--		uSettings[ v ]= true
--	end

	--
	-- all command line settings overwrite config file settings
	--
	for k, v in pairs( clSettings )
	do
		if k ~= 'syncs'
		then
			uSettings[ k ] = v
		end
	end

	--
	-- implicitly forces 'insist' on Lsyncd resets.
	--
	if not firstTime
	then
		uSettings.insist = true
	end

	--
	-- adds syncs specified by command line.
	--
	if clSettings.syncs
	then
		for _, s in ipairs( clSettings.syncs )
		do
			if s[ 1 ] == 'rsync'
			then
				sync{
					default.rsync,
					source = s[ 2 ],
					target = s[ 3 ]
				}
			elseif s[ 1 ] == 'rsyncssh'
			then
				sync{
					default.rsyncssh,
					source = s[ 2 ],
					host   = s[ 3 ],
					targetdir=s[ 4 ]
				}
			elseif s[ 1 ] == 'direct'
			then
				sync{
					default.direct,
					source=s[ 2 ],
					target=s[ 3 ]
				}
			end
		end
	end

	if uSettings.nodaemon
	then
		lsyncd.configure( 'nodaemon' )
	end

	if uSettings.logfile
	then
		lsyncd.configure( 'logfile', uSettings.logfile )
	end

	if uSettings.logident
	then
		lsyncd.configure( 'logident', uSettings.logident )
	end

	if uSettings.logfacility
	then
		lsyncd.configure( 'logfacility', uSettings.logfacility )
	end

	if uSettings.pidfile
	then
		lsyncd.configure( 'pidfile', uSettings.pidfile )
	end

	--
	-- Transfers some defaults to uSettings
	--
	if uSettings.statusInterval == nil
	then
		uSettings.statusInterval = default.statusInterval
	end

	-- makes sure the user gave Lsyncd anything to do
	if Syncs.size() == 0
	then
		log(
			'Error',
			'Nothing to watch!'
		)

		os.exit( -1 )
	end

	-- from now on use logging as configured instead of stdout/err.
	lsyncdStatus = 'run';

	lsyncd.configure( 'running' );

	local ufuncs = {
		'onAttrib',
		'onCreate',
		'onDelete',
		'onModify',
		'onMove',
		'onStartup',
	}

	-- translates layer 3 scripts
	for _, s in Syncs.iwalk()
	do
		-- checks if any user functions is a layer 3 string.
		local config = s.config

		for _, fn in ipairs( ufuncs )
		do
			if type(config[fn]) == 'string'
			then
				local ft = functionWriter.translate( config[ fn ] )

				config[ fn ] = assert( loadstring( 'return '..ft ) )( )
			end
		end
	end

	-- runs through the Syncs created by users
	for _, s in Syncs.iwalk( )
	do
		if s.config.monitor == 'inotify'
		then
			Inotify.addSync( s, s.source )
		elseif s.config.monitor == 'fsevents'
		then
			Fsevents.addSync( s, s.source )
		else
			error(
				'sync ' ..
				s.config.name ..
				' has no known event monitor interface.'
			)
		end

		-- if the sync has an init function, the init delay
		-- is stacked which causes the init function to be called.
		if s.config.init
		then
			s:addInitDelay( )
		end
	end

end

--
-- Called by core to query the soonest alarm.
--
-- @return false ... no alarm, core can go in untimed sleep
--         true  ... immediate action
--         times ... the alarm time (only read if number is 1)
--
function runner.getAlarm
( )

	log( 'Function', 'getAlarm( )' )

	if lsyncdStatus ~= 'run'
	then
		return false
	end

	local alarm = false

	--
	-- Checks if 'a' is sooner than the 'alarm' up-value.
	--
	local function checkAlarm
	(
		a
	)
		if a == nil
		then
			error('got nil alarm')
		end

		if alarm == true or not a
		then
			-- 'alarm' is already immediate or
			-- a not a new alarm
			return
		end

		-- sets 'alarm' to a if a is sooner
		if not alarm or a < alarm
		then
			alarm = a
		end
	end

	--
	-- checks all syncs for their earliest alarm,
	-- but only if the global process limit is not yet reached.
	--
	if not uSettings.maxProcesses
	or processCount < uSettings.maxProcesses
	then
		for _, s in Syncs.iwalk( )
		do
			checkAlarm( s:getAlarm( ) )
		end
	else
		log(
			'Alarm',
			'at global process limit.'
		)
	end

	-- checks if a statusfile write has been delayed
	checkAlarm( StatusFile.getAlarm( ) )

	-- checks for an userAlarm
	checkAlarm( UserAlarms.getAlarm( ) )

	log(
		'Alarm',
		'runner.getAlarm returns: ',
		alarm
	)

	return alarm

end


--
-- Called when an file system monitor events arrive
--
runner.inotifyEvent = Inotify.event
runner.fsEventsEvent = Fsevents.event

--
-- Collector for every child process that finished in startup phase
--
function runner.collector
(
	pid,       -- pid of the child process
	exitcode   -- exitcode of the child process
)
	if exitcode ~= 0
	then
		log(
			'Error',
			'Startup process',
			pid,
			' failed'
		)

		terminate( -1 )
	end

	return 0
end

--
-- Called by core when an overflow happened.
--
function runner.overflow( )

	log(
		'Normal',
		'--- OVERFLOW in event queue ---'
	)

	lsyncdStatus = 'fade'

end

--
-- Called by core on a hup signal.
--
function runner.hup( )

	log(
		'Normal',
		'--- HUP signal, resetting ---'
	)

	lsyncdStatus = 'fade'

end

--
-- Called by core on a term signal.
--
function runner.term( sigcode )

	local sigtexts = {
		[ 2 ] =
			'INT',

		[ 15 ] =
			'TERM'
	};

	local sigtext = sigtexts[ sigcode ];

	if not sigtext then
		sigtext = 'UNKNOWN'
	end

	log(
		'Normal',
		'--- ', sigtext, ' signal, fading ---'
	)

	lsyncdStatus = 'fade'

end

--============================================================================
-- Lsyncd runner's user interface
--============================================================================

--
-- Main utility to create new observations.
--
-- Returns an Inlet to that sync.
--
function sync( opts )

	if lsyncdStatus ~= 'init' then
		error(
			'Sync can only be created during initialization.',
			2
		)
	end

	return Syncs.add( opts ).inlet

end


--
-- Spawns a new child process.
--
function spawn(
	agent,  -- the reason why a process is spawned.
	        -- a delay or delay list for a sync
	        -- it will mark the related files as blocked.
	binary, -- binary to call
	...     -- arguments
)
	if
		agent == nil or
		type( agent ) ~= 'table'
	then
		error(
			'spawning with an invalid agent',
			2
		)
	end

	if lsyncdStatus == 'fade' then
		log(
			'Normal',
			'ignored process spawning while fading'
		)
		return
	end

	if type( binary ) ~= 'string' then
		error(
			'calling spawn(agent, binary, ...): binary is not a string',
			2
		)
	end

	local dol = InletFactory.getDelayOrList( agent )

	if not dol then
		error(
			'spawning with an unknown agent',
			2
		)
	end

	--
	-- checks if a spawn is called on an already active event
	--
	if dol.status
	then
		-- is an event

		if dol.status ~= 'wait'
		then
			error('spawn() called on an non-waiting event', 2)
		end
	else
		-- is a list
		for _, d in ipairs( dol )
		do
			if d.status ~= 'wait'
			and d.status ~= 'block'
			then
				error( 'spawn() called on an non-waiting event list', 2 )
			end
		end
	end

	--
	-- tries to spawn the process
	--
	local pid = lsyncd.exec( binary, ... )

	if pid and pid > 0
	then
		processCount = processCount + 1

		if
			uSettings.maxProcesses and
			processCount > uSettings.maxProcesses
		then
			error( 'Spawned too much processes!' )
		end

		local sync = InletFactory.getSync( agent )

		-- delay or list
		if dol.status
		then
			-- is a delay
			dol:setActive( )

			sync.processes[ pid ] = dol
		else
			-- is a list
			for _, d in ipairs( dol )
			do
				d:setActive( )
			end

			sync.processes[ pid ] = dol
		end
	end
end

--
-- Spawns a child process using the default shell.
--
function spawnShell
(
	agent,     -- the delay(list) to spawn the command for
	command,   -- the shell command
	...        -- additonal arguments
)
	return spawn(
		agent,
		'/bin/sh',
		'-c',
		command,
		'/bin/sh',
		...
	)
end


--
-- Observes a filedescriptor.
--
function observefd
(
	fd,     -- file descriptor
	ready,  -- called when fd is ready to be read
	writey  -- called when fd is ready to be written
)
	return lsyncd.observe_fd(
		fd,
		ready,
		writey
	)
end


--
-- Stops observeing a filedescriptor.
--
function nonobservefd
(
	fd      -- file descriptor
)
	return lsyncd.nonobserve_fd( fd )
end


--
-- Calls func at timestamp.
--
-- Use now() to receive current timestamp
-- add seconds with '+' to it
--
alarm = UserAlarms.alarm


--
-- Comfort routine also for user.
-- Returns true if 'String' starts with 'Start'
--
function string.starts
(
	String,
	Start
)
	return string.sub( String, 1, #Start )==Start
end


--
-- Comfort routine also for user.
-- Returns true if 'String' ends with 'End'
--
function string.ends
(
	String,
	End
)
	return End == '' or string.sub( String, -#End ) == End
end


--
-- The settings call
--
function settings
(
	a1  -- a string for getting a setting
	--     or a table of key/value pairs to set these settings
)

	-- if a1 is a string this is a get operation
	if type( a1 ) == 'string'
	then
		return uSettings[ a1 ]
	end

	-- if its a table it sets all the value of the bale
	for k, v in pairs( a1 )
	do
		if type( k ) ~= 'number'
		then
			if not settingsCheckgauge[ k ]
			then
				error(
					'setting "'
					..k
					..'" unknown.',
					2
				)
			end

			uSettings[ k ] = v
		else
			if not settingsCheckgauge[ v ]
			then
				error(
					'setting "'
					..v
					..'" unknown.',
					2
				)
			end

			uSettings[ v ] = true
		end
	end
end

settingsSafe = settings

--
-- Returns the core the runners function interface.
--
return runner
