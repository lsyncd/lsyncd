--
-- sync.lua from Lsyncd -- the Live (Mirror) Syncing Demon
--
--
-- Holds information about one observed directory including subdirs.
--
--
-- This code assumes your editor is at least 100 chars wide.
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
-- Syncs that have no name specified by the user script
-- get an incremental default name 'Sync[X]'
--
local nextDefaultName = 1


--
-- Appends a filter to the sync
--
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

	if self.onCollect
	then
		for _, func in ipairs( self.onCollect )
		do
			func( self:getUserIntf( ) )
		end
	end
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
	if self.stopped
	or #self.processes >= self.config.maxProcesses
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

	if self.stopped
	or #self.processes >= self.config.maxProcesses
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
-- Returns a user interface for this sync
--
local function getUserIntf
(
	self
)
	local ui = self.userIntf

	if ui then return ui end

	ui = {
		-- Stops the sync, meaning no more
		-- processes will be spawned
		stop = function( )
			self.stopped = true
		end,

		-- Registers an additional function to be called
		-- after each collect.
		--
		-- Used by default signal handlers to wait
		-- for children to finish (or react on forwarded signal)
		onCollect = function( func )
			if not self.onCollect
			then
				self.onCollect = { func }
			else
				table.insert( self.onCollect, func )
			end
		end,

		-- Returns a list of pids of children
		-- processes
		pids = function
		( )
			return self.processes:copy( )
		end,

		processCount = function
		( )
			return #self.processes
		end,
	}

	self.userIntf = ui

	return ui
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
		config    = config,
		delays    = Queue.new( ),
		source    = config.source,
		processes = Counter.new( ),
		filters   = nil,
		stopped   = false,

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

		-- use interface
		getUserIntf     = getUserIntf,
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
-- Exported interface.
--
Sync = { new = new }

