--~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
-- combiner.lua   Live (Mirror) Syncing Demon
--
--
-- Combines delays.
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
local combineNoMove =
{
	Attrib =
	{
		Attrib = 'absorb',
		Modify = 'replace',
		Create = 'replace',
		Delete = 'replace'
	},

	Modify =
	{
		Attrib = 'absorb',
		Modify = 'absorb',
		Create = 'replace',
		Delete = 'replace'
	},

	Create =
	{
		Attrib = 'absorb',
		Modify = 'absorb',
		Create = 'absorb',
		Delete = 'replace'
	},

	Delete =
	{
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
	log( 'Delay', d2.etype, ': ',d2.path, ' absorbed by ', d1.etype,': ',d1.path )
end

--
-- The new delay replaces the old one if it's a file.
--
local function logReplace
(
	d1, -- old delay
	d2  -- new delay
)
	log( 'Delay', d2.etype, ': ', d2.path, ' replaces ', d1.etype, ': ', d1.path )
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
		'Delay', d2.etype, ': ', d2.path, ' -> ', d2.path2,
		' splits on ', d1.etype, ': ', d1.path
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

	if d1.active then active = 'active ' end

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
Combiner = { combine = combine, log = log }

