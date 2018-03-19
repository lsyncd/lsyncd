--~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
-- lsyncd.lua   Live (Mirror) Syncing Demon
--
--
-- Creates inlets for syncs: the user interface for events.
--
--
-- This code assumes your editor is at least 100 chars wide.
--
-- License: GPLv2 (see COPYING) or any later version
-- Authors: Axel Kittenberger <axkibe@gmail.com>
--
--~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~


if lsyncd_version
then
	print( 'Error, Lsyncd mantle already loaded' )
	os.exit( -1 )
end


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
local eventFields =
{
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
		if eu then return eu end

		local event = { }

		setmetatable( event, eventMeta )

		e2d[ event ]  = delay

		e2d2[ delay ] = event

		return event
	else
		-- moves have 2 events - origin and destination
		if eu then return eu[1], eu[2] end

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

	if eu then return eu end

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
	-- Appens a filter.
	--
	appendFilter = function
	(
		sync,   -- the sync of the inlet
		rule,   -- '+' or '-'
		pattern -- exlusion pattern to add
	)
		sync:appendFilter( rule, pattern )
	end,

	--
	-- Gets the list of filters and excldues
	-- as rsync-like filter/patterns form.
	--
	getFilters = function
	(
		sync -- the sync of the inlet
	)
		-- creates a copy
		local e = { }
		local en = 1;

		if sync.filters
		then
			for _, entry in ipairs( sync.filters.list )
			do
				e[ en ] = entry.rule .. ' ' .. entry.pattern;
				en = en + 1;
			end
		end

		return e;
	end,

	--
	-- Returns true if the sync has filters
	--
	hasFilters = function
	(
		sync -- the sync of the inlet
	)
		return not not sync.filters
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
		return d2e( sync:getNextDelay( core.now( ) ) )
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
			error( 'inlet does not have function "'..func..'"', 2 )
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
-- Exported interface.
--
InletFactory = {
	getDelayOrList = getDelayOrList,
	d2e            = d2e,
	dl2el          = dl2el,
	getSync        = getSync,
	newInlet       = newInlet,
}

