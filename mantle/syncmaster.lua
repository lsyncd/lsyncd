--
-- syncmaster.lua from Lsyncd -- the Live (Mirror) Syncing Demon
--
--
-- The singleton SyncMaster maintains all configured syncs.
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


--
-- Metatable.
--
local mt = { }


--
-- the list of all syncs
--
local syncList = Array.new( )


--
-- Returns the number of syncs.
--
mt.__len = function
( )
	return #syncList
end


--
-- Walks the syncs.
--
mt.__ipairs = function
( )
	return ipairs( syncList )
end


--
-- The round robin counter. In case of global limited maxProcesses
-- gives every sync equal chances to spawn the next process.
--
local round = 0


--
-- The cycle( ) sheduler goes into the next round of roundrobin.
--
local function nextRound
( )
	round = round + 1;

	if round >= #syncList then round = 0 end

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
	return syncList[ i ]
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
				type( k ) ~= 'number'
				or verbatim
				or cs._verbatim == true
			)
			and
			(
				type( cs._merge ) ~= 'table'
				or cs._merge[ k ] == true
			)
		then
			inheritKV( cd, k, v )
		end
	end

	-- recursevely inherits all integer keyed tables
	-- ( for non-verbatim tables )
	if cs._verbatim ~= true
	then
		for k, v in ipairs( cs )
		do
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
	if k == '_merge' or k == '_verbatim' then return end

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
	-- Creates a new config table which inherits all keys/values
	-- from integer keyed tables
	local uconfig = config

	config = { }

	inherit( config, uconfig )

	-- last and least defaults are inherited
	inherit( config, userenv.default )

	local inheritSettings =
	{
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

	if not config[ 'source' ]
	then
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
	local realsrc = core.realdir( config.source )

	if not realsrc
	then
		log( 'Error', 'Cannot access source directory: ', config.source )
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

		log( 'Error', info.short_src, ':', info.currentline, ': no actions specified.' )

		terminate( -1 )
	end

	-- the monitor to use
	config.monitor = uSettings.monitor or config.monitor or Monitor.default( )

	if config.monitor ~= 'inotify'
	then
		local info = debug.getinfo( 3, 'Sl' )

		log(
			'Error',
			info.short_src, ':', info.currentline,
			': event monitor "', config.monitor, '" unknown.'
		)

		terminate( -1 )
	end

	-- creates the new sync
	local s = Sync.new( config )

	syncList:push( s )

	return s
end


--
-- Tests if any sync is interested in a path.
--
local function concerns
(
	path
)
	for _, s in ipairs( syncList )
	do
		if s:concerns( path )
		then
			return true
		end
	end

	return false
end

--
-- Exported interface.
--
SyncMaster =
{
	add = add,   -- FIXME forward through metatable
	get = get,   -- FIXME forward through metatable
	getRound = getRound,
	concerns = concerns,
	nextRound = nextRound
}

setmetatable( SyncMaster, mt )

