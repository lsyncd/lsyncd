--~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
-- lsyncd.lua   Live (Mirror) Syncing Demon
--
--
-- This is the "runner" part of Lsyncd. It containts all its high-level logic.
-- It works closely together with the Lsyncd core in lsyncd.c. This means it
-- cannot be runned directly from the standard lua interpreter.
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
-- Safes mantle stuff wrapped away from user scripts
--
local core = core
local lockGlobals = lockGlobals
local Inotify = Inotify
local Array = Array
local Queue = Queue
local Combiner = Combiner
local Delay = Delay
local InletFactory = InletFactory
local Filter = Filter


--
-- Shortcuts (which user is supposed to be able to use them as well)
--
log       = core.log
terminate = core.terminate
now       = core.now
readdir   = core.readdir


--
-- Coping globals to ensure userscripts cannot change this.
--
local log       = log
local terminate = terminate
local now       = now
local readdir   = readdir
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
	statusFile     = true,
	statusInterval = true,
	logfacility    = true,
	logident       = true,
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
-- FIXME exported global!
uSettings = { }


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
-- Holds information about one observed directory including subdirs.
--
local Sync = ( function
( )
	--
	-- Syncs that have no name specified by the user script
	-- get an incremental default name 'Sync[X]'
	--
	local nextDefaultName = 1

	local function appendFilter
	(
		self,
		rule,
		pattern
	)
		if not self.filters then self.filters = Filters.new( ) end

		return self.filters:append( rule, pattern )
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
	-- Returns false if the relative path is filtered
	--
	local function testFilter
	(
		self,   -- the Sync
		path    -- the relative path
	)
		-- never filter the relative root itself
		-- ( that would make zero sense )
		if path == '/'
		or not self.filters
		then
			return true
		end

		return self.filters:test( path )
	end

	--
	-- Returns true if this Sync concerns about 'path'.
	--
	local function concerns
	(
		self,    -- the Sync
		path     -- the absolute path
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

		return testFilter( self, path:sub( #self.source ) )
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

		-- not a child of this sync?
		if not delay then return end

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
				if alarm < 1 then alarm = 1 end

				delay:wait( now( ) + alarm )
			end
		else
			log( 'Delay', 'collected a list' )

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

				-- delays are at least 1 second
				if alarm < 1 then alarm = 1 end

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
				local entries = core.readdir( self.source .. path )

				if entries
				then
					for dirname, isdir in pairs( entries )
					do
						local pd = path .. dirname

						if isdir then pd = pd..'/' end

						log( 'Delay', 'Create creates Create on ', pd )

						delay( self, 'Create', time, pd, nil )
					end
				end
			end
		end

		-- exclusion tests
		if not path2
		then
			-- simple test for single path events
			if not testFilter( self, path )
			then
				log( 'Filter', 'filtered ', etype, ' on "', path, '"' )

				return
			end
		else
			-- for double paths ( move ) it might result into a split
			local ex1 = not testFilter( self, path )

			local ex2 = not testFilter( self, path2 )

			if ex1 and ex2
			then
				log(
					'Filter',
					'filtered "', etype, ' on "', path,
					'" -> "', path2, '"'
				)

				return
			elseif not ex1 and ex2
			then
				-- splits the move if only partly filtered
				log(
					'Filter',
					'filtered destination transformed ',
					etype,
					' to Delete ',
					path
				)

				 delay( self, 'Delete', time, path, nil )

				return
			elseif ex1 and not ex2
			then
				-- splits the move if only partly filtered
				log(
					'Filter',
					'filtered origin transformed ',
					etype,
					' to Create.',
					path2
				)

				delay( self, 'Create', time, path2, nil )

				return
			end
		end

		if etype == 'Move'
		and not self.config.onMove
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
		local nd = Delay.new( etype, self, alarm, path, path2 )

		if nd.etype == 'Init' or nd.etype == 'Blanket'
		then
			-- always stack init or blanket events on the last event
			log(
				'Delay',
				'Stacking ',
				nd.etype,
				' event.'
			)

			if #self.delays > 0
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
		if #self.processes >= self.config.maxProcesses
		then
			return false
		end

		-- first checks if more processes could be spawned
		-- finds the nearest delay waiting to be spawned
		for _, d in self.delays:qpairs( )
		do
			if d.status == 'wait'
			then
				return d.alarm
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

			if tr == 'break' then break end

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
		log( 'Function', 'invokeActions( "', self.config.name, '", ', timestamp, ' )' )

		if #self.processes >= self.config.maxProcesses
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
				log( 'Alarm', 'at global process limit.' )
				return
			end

			if #self.delays < self.config.maxDelays
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

				if #self.processes >= self.config.maxProcesses
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
			if #self.delays < self.config.maxDelays
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

		f:write( 'There are ', #self.delays, ' delays\n')

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

		f:write( 'Filtering:\n' )

		local nothing = true

		if self.filters
		then
			for _, e in pairs( self.filters.list )
			do
				nothing = false

				f:write( e.rule, ' ', e.pattern,'\n' )
			end
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
	local function new
	(
		config
	)
		local s =
		{
			-- fields
			config = config,
			delays = Queue.new( ),
			source = config.source,
			processes = Counter.new( ),
			filters = nil,

			-- functions
			addBlanketDelay = addBlanketDelay,
			addExclude      = addExclude,
			addInitDelay    = addInitDelay,
			appendFilter    = appendFilter,
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

		-- loads filters
		if config.filter
		then
			local te = type( config.filter )

			s.filters = Filters.new( )

			if te == 'table'
			then
				s.filters:appendList( config.filter )
			elseif te == 'string'
			then
				s.filters:append( config.filter )
			else
				error( 'type for filter must be table or string', 2 )
			end

		end

		if config.delay ~= nil
		and ( type( config.delay ) ~= 'number' or config.delay < 0 )
		then
			error( 'delay must be a number and >= 0', 2 )
		end

		if config.filterFrom
		then
			if not s.filters then s.filters = Filters.new( ) end

			s.filters:loadFile( config.filterFrom )
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
Syncs = ( function
( )
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
		return syncList[ i ];
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
		-- Checks if user overwrote the settings function.
		-- ( was Lsyncd < 2.1 style )
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
		config.monitor =
			uSettings.monitor
			or config.monitor
			or Monitors.default( )

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
	-- Allows a for-loop to walk through all syncs.
	--
	local function iwalk
	( )
		return ipairs( syncList )
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
	-- Public interface
	--
	local intf =
	{
		add = add,
		get = get,
		getRound = getRound,
		concerns = concerns,
		iwalk = iwalk,
		nextRound = nextRound
	}

	setmetatable( intf, mt )

	return intf
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
	local function default
	( )
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

		for ia, iv in ipairs( args )
		do
			-- a list of arguments this arg is being split into
			local a = { { true, iv } }

			-- goes through all translates
			for _, v in ipairs( transVars )
			do
				local ai = 1
				while ai <= #a
				do
					if a[ ai ][ 1 ]
					then
						local pre, post =
							string.match( a[ ai ][ 2 ], '(.*)'..v[1]..'(.*)' )

						if pre
						then
							if v[3] > 1
							then
								haveEvent2 = true
							end

							if pre ~= ''
							then
								table.insert( a, ai, { true, pre } )
								ai = ai + 1
							end

							a[ ai ] = { false, v[ 2 ] }

							if post ~= ''
							then
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

			for _, v in ipairs( a )
			do
				if not first then as = as..' .. ' end

				if v[ 1 ]
				then
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
				function
				( )
					occur = true
					return '"$' .. argn .. '"'
				end
			)

			lc = string.gsub( lc, v[1], ']]..' .. v[2] .. '..[[' )

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

		log( 'FWrite', 'translated "', str, '" to \n', ft )

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
		-- takes care not to write too often
		--
		if uSettings.statusInterval > 0
		then
			-- already waiting?
			if alarm and timestamp < alarm
			then
				log( 'Statusfile', 'waiting(', timestamp, ' < ', alarm, ')' )

				return
			end

			-- determines when a next write will be possible
			if not alarm
			then
				local nextWrite = lastWritten and timestamp + uSettings.statusInterval

				if nextWrite and timestamp < nextWrite
				then
					log( 'Statusfile', 'setting alarm: ', nextWrite )
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
	return { write = write, getAlarm = getAlarm }
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

		local a =
		{
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
	local function invoke
	(
		timestamp
	)
		while #alarms > 0
		and alarms[ 1 ].timestamp <= timestamp
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
runner = { }


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
	log( 'Error', 'in Lua: ', message )

	-- prints backtrace
	local level = 2

	while true
	do
		local info = debug.getinfo( level, 'Sl' )

		if not info then terminate( -1 ) end

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


-- Registers the mantle with the core
core.mantle( runner )


--
-- Called from core whenever a child process has finished and
-- the zombie process was collected by core.
--
function runner.collectProcess
(
	pid,       -- process id
	exitcode   -- exitcode
)
	processCount = processCount - 1

	if processCount < 0
	then
		error( 'negative number of processes!' )
	end

	for _, s in Syncs.iwalk( )
	do
		if s:collect( pid, exitcode ) then return end
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
			if lastReportedWaiting == false
			or timestamp >= lastReportedWaiting + 60
			then
				lastReportedWaiting = timestamp

				log( 'Normal', 'waiting for ', processCount, ' more child processes.' )
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

			if ir >= #Syncs then ir = 0 end
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
 lsyncd [OPTIONS] [CONFIG-FILE]

OPTIONS:
  -delay SECS         Overrides default delay times
  -help               Shows this
  -log    all         Logs everything (debug)
  -log    scarce      Logs errors only
  -log    [Category]  Turns on logging for a debug category
  -logfile FILE       Writes log to FILE (DEFAULT: uses syslog)
  -version            Prints versions and exits

LICENSE:
  GPLv2 or any later version.

SEE:
  `man lsyncd` or visit https://axkibe.github.io/lsyncd/ for further information.
]])

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
	local options =
	{
		-- log is handled by core already.

		delay =
		{
			1,
			function
			(
				secs
			)
				clSettings.delay = secs + 0
			end
		},

		log = { 1, nil },

		logfile =
		{
			1,
			function
			(
				file
			)
				clSettings.logfile = file
			end
		},

		version =
		{
			0,
			function
			( )
				io.stdout:write( 'Version: ', lsyncd_version, '\n' )

				os.exit( 0 )
			end
		}
	}

	-- non-opts is filled with all args that were no part dash options
	local nonopts = { }

	local i = 1

	while i <= #args
	do
		local a = args[ i ]

		if a:sub( 1, 1 ) ~= '-'
		then
			table.insert( nonopts, args[ i ] )
		else
			if a:sub( 1, 2 ) == '--'
			then
				a = a:sub( 3 )
			else
				a = a:sub( 2 )
			end

			local o = options[ a ]

			if not o
			then
				log( 'Error', 'unknown option command line option ', args[ i ] )

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

	if #nonopts == 0
	then
		runner.help( args[ 0 ] )
	elseif #nonopts == 1
	then
		return nonopts[ 1 ]
	else
		-- TODO make this possible
		log( 'Error', 'There can only be one config file in the command line.' )

		os.exit( -1 )
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
			'      please use settings{ ... } ( without the equal sign )'
		)

		os.exit( -1 )
	end

	lastReportedWaiting = false

	--
	-- From this point on, no globals may be created anymore
	--
	lockGlobals( )

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

	if uSettings.logfile
	then
		core.configure( 'logfile', uSettings.logfile )
	end

	if uSettings.logident
	then
		core.configure( 'logident', uSettings.logident )
	end

	if uSettings.logfacility
	then
		core.configure( 'logfacility', uSettings.logfacility )
	end

	--
	-- Transfers some defaults to uSettings
	--
	if uSettings.statusInterval == nil
	then
		uSettings.statusInterval = default.statusInterval
	end

	-- makes sure the user gave Lsyncd anything to do
	if #Syncs == 0
	then
		log( 'Error', 'Nothing to watch!' )
		os.exit( -1 )
	end

	-- from now on use logging as configured instead of stdout/err.
	lsyncdStatus = 'run';

	core.configure( 'running' );

	local ufuncs =
	{
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

				config[ fn ] = assert( load( 'return '..ft ) )( )
			end
		end
	end

	-- runs through the Syncs created by users
	for _, s in Syncs.iwalk( )
	do
		if s.config.monitor == 'inotify'
		then
			Inotify.addSync( s, s.source )
		else
			error( 'sync '.. s.config.name..' has unknown event monitor interface.' )
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

	if lsyncdStatus ~= 'run' then return false end

	local alarm = false

	--
	-- Checks if 'a' is sooner than the 'alarm' up-value.
	--
	local function checkAlarm
	(
		a  -- alarm time
	)
		if a == nil then error( 'got nil alarm' ) end

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

	log( 'Alarm', 'runner.getAlarm returns: ', alarm )

	return alarm
end


--
-- Called when an file system monitor events arrive
--
runner.inotifyEvent = Inotify.event

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
		log( 'Error', 'Startup process', pid, ' failed' )

		terminate( -1 )
	end

	return 0
end

--
-- Called by core when an overflow happened.
--
function runner.overflow
( )
	log( 'Normal', '--- OVERFLOW in event queue ---' )

	lsyncdStatus = 'fade'
end

--
-- Called by core on a hup signal.
--
function runner.hup
( )
	log( 'Normal', '--- HUP signal, resetting ---' )

	lsyncdStatus = 'fade'
end

--
-- Called by core on a term signal.
--
function runner.term
(
	sigcode  -- signal code
)
	local sigtexts =
	{
		[ 2 ] = 'INT',
		[ 15 ] = 'TERM'
	};

	local sigtext = sigtexts[ sigcode ];

	if not sigtext then sigtext = 'UNKNOWN' end

	log( 'Normal', '--- ', sigtext, ' signal, fading ---' )

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
function sync
(
	opts
)
	if lsyncdStatus ~= 'init'
	then
		error( 'Sync can only be created during initialization.', 2 )
	end

	return Syncs.add( opts ).inlet
end


--
-- Spawns a new child process.
--
function spawn
(
	agent,  -- the reason why a process is spawned.
	        -- a delay or delay list for a sync
	        -- it will mark the related files as blocked.
	binary, -- binary to call
	...     -- arguments
)
	if agent == nil
	or type( agent ) ~= 'table'
	then
		error( 'spawning with an invalid agent', 2 )
	end

	if lsyncdStatus == 'fade'
	then
		log( 'Normal', 'ignored process spawning while fading' )
		return
	end

	if type( binary ) ~= 'string'
	then
		error( 'calling spawn(agent, binary, ...): binary is not a string', 2 )
	end

	local dol = InletFactory.getDelayOrList( agent )

	if not dol
	then
		error( 'spawning with an unknown agent', 2 )
	end

	--
	-- checks if a spawn is called on an already active event
	--
	if dol.status
	then
		-- is an event

		if dol.status ~= 'wait'
		then
			error( 'spawn() called on an non-waiting event', 2 )
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
	local pid = core.exec( binary, ... )

	if pid and pid > 0
	then
		processCount = processCount + 1

		if uSettings.maxProcesses
		and processCount > uSettings.maxProcesses
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
	return spawn( agent, '/bin/sh', '-c', command, '/bin/sh', ... )
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
	return core.observe_fd( fd, ready, writey )
end


--
-- Stops observeing a filedescriptor.
--
function nonobservefd
(
	fd      -- file descriptor
)
	return core.nonobserve_fd( fd )
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
				error( 'setting "'..k..'" unknown.', 2 )
			end

			uSettings[ k ] = v
		else
			if not settingsCheckgauge[ v ]
			then
				error( 'setting "'..v..'" unknown.', 2 )
			end

			uSettings[ v ] = true
		end
	end
end

settingsSafe = settings

