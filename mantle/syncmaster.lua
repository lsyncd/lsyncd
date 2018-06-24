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
-- Returns sync at listpos key
--
mt.__index = function
(
	self,
	key
)
	if type( key ) ~= 'number' then error( "key not a number" ) end

	return syncList[ key ]
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



local inherit


--
-- Inherits the contents of all tables with array keys
-- Returns the table with flattened inhertance,
-- also returns array size
--
--
local function flattenInheritance
(
	t
)
	local tf = { }

	inherit( tf, t )

	for k, v in ipairs( t )
	do
		-- numbers as key and table as value
		-- means recursive inherit
		if type( v ) == 'table'
		then
			local vv = flattenInheritance( v )
			inherit( tf, vv )
		else
			if tf[ k ] == nil then tf[ k ] = v end
		end
	end

	return tf
end


--
-- Recursevly inherits a source table to a destionation table
-- copying all keys from source.
--
-- All entries with integer keys are inherited as additional
-- sources for non-verbatim tables
--
inherit = function
(
	cd, -- table copy destination
	cs  -- table copy source
)
	local imax = 0

	for k, _ in ipairs( cs ) do imax = k end

	for k, v in pairs( cs )
	do
		if type( k ) == 'number'
		then
			if( k < 1 or k > imax or math.floor( k ) ~= k )
			then
				-- not an array integer
				if type( v ) == 'table'
				then
					error( 'non sequence numeric key used as inheritance', 2 )
				end

				if cd[ k ] == nil then cd[ k ] = v end
			end
		else
			if type( v ) == 'table'
			then
				v = flattenInheritance( v )
			end

			local dv = cd[ k ]

			if dv == nil
			then
				cd[ k ] = v
			elseif type( dv ) == 'table'
			and type( v ) == 'table'
			and v._merge ~= false
			then
				dv = inherit( { }, dv )
				dv = inherit( dv, v )
				cd[ k ] = dv
			end
		end
	end

	return cd
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
	config = flattenInheritance( config )

	-- last and least default prototype is inherited
	inherit( config, userenv.default.proto )

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
-- Removes a sync.
--
-- FUTURE also allow instead the userIntf the sync name
--
local function remove
(
	syncUserIntf
)
	-- finds the sync
	local pos, sync
	for p, s in ipairs( syncList )
	do
		if s.userIntf == syncUserIntf
		then
			pos = p
			sync = s
		end
	end

	if not sync
	then
		log( 'Error', 'To be removed sync not found.' )
		terminate( -1 )
	end

	if #sync.processes ~= 0
	then
		log( 'Error', 'To be removed sync still has child processes.' )
		terminate( -1 )
	end

	if pos >= round and round > 0 then round = round - 1 end

	syncList:remove( pos )
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
		if s:concerns( path ) then return true end
	end

	return false
end


--
-- Returns a copy of the sync list.
--
local function syncListCopy
( )
	return syncList:copy( )
end

--
-- Exported interface.
--
SyncMaster =
{
	add = add,
	remove = remove,
	getRound = getRound,
	concerns = concerns,
	nextRound = nextRound,
	syncList = syncListCopy,
}

setmetatable( SyncMaster, mt )

