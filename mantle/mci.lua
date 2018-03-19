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
-- Shortcuts
--
configure = core.configure
log       = core.log
terminate = core.terminate
now       = core.now
readdir   = core.readdir


--
-- Global: total number of processess running.
--
processCount = 0


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
clSettings = { }


--
-- Settings specified by config scripts.
--
uSettings = { }



--============================================================================
-- Mantle core interface. These functions are called from core.
--============================================================================


--
-- Current status of Lsyncd.
--
-- 'init'  ... on (re)init
-- 'run'   ... normal operation
-- 'fade'  ... waits for remaining processes
--
lsyncdStatus = 'init'


--
-- The mantle cores interface
--
mci = { }


--
-- Last time said to be waiting for more child processes
--
local lastReportedWaiting = false

--
-- Called from core whenever Lua code failed.
--
-- Logs a backtrace
--
function mci.callError
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
			'Error', 'Backtrace ',
			level - 1, ' :',
			info.short_src, ':',
			info.currentline
		)

		level = level + 1
	end
end


-- Registers the mantle with the core
core.mci( mci )


--
-- Called from core whenever a child process has finished and
-- the zombie process was collected by core.
--
function mci.collectProcess
(
	pid,       -- process id
	exitcode   -- exitcode
)
	processCount = processCount - 1

	if processCount < 0
	then
		error( 'negative number of processes!' )
	end

	for _, s in SyncMaster.iwalk( )
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
function mci.cycle
(
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
		error( 'mci.cycle() called while not running!' )
	end

	--
	-- Goes through all syncs and spawns more actions
	-- if possibly. But only lets SyncMaster invoke actions if
	-- not at global limit.
	--
	if not uSettings.maxProcesses
	or processCount < uSettings.maxProcesses
	then
		local start = SyncMaster.getRound( )

		local ir = start

		repeat
			local s = SyncMaster.get( ir )

			s:invokeActions( timestamp )

			ir = ir + 1

			if ir >= #SyncMaster then ir = 0 end
		until ir == start

		SyncMaster.nextRound( )
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
function mci.help( )
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
function mci.configure(
	args,     -- arguments given by user
	monitors  -- list of monitors the core can do
)
	Monitor.initialize( monitors )

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
		mci.help( args[ 0 ] )
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
function mci.initialize( firstTime )

	-- Checks if user overwrote the settings function.
	-- ( was Lsyncd <2.1 style )
	if userENV.settings ~= settings
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
		configure( 'logfile', uSettings.logfile )
	end

	if uSettings.logident
	then
		configure( 'logident', uSettings.logident )
	end

	if uSettings.logfacility
	then
		configure( 'logfacility', uSettings.logfacility )
	end

	--
	-- Transfers some defaults to uSettings
	--
	if uSettings.statusInterval == nil
	then
		uSettings.statusInterval = default.statusInterval
	end

	-- makes sure the user gave Lsyncd anything to do
	if #SyncMaster == 0
	then
		log( 'Error', 'Nothing to watch!' )
		os.exit( -1 )
	end

	-- from now on use logging as configured instead of stdout/err.
	lsyncdStatus = 'run';

	configure( 'running' );

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
	for _, s in SyncMaster.iwalk()
	do
		-- checks if any user functions is a layer 3 string.
		local config = s.config

		for _, fn in ipairs( ufuncs )
		do
			if type(config[fn]) == 'string'
			then
				local ft = FWriter.translate( config[ fn ] )

				config[ fn ] = assert( load( 'return '..ft ) )( )
			end
		end
	end

	-- runs through the Syncs created by users
	for _, s in SyncMaster.iwalk( )
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
function mci.getAlarm
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
		for _, s in SyncMaster.iwalk( )
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

	log( 'Alarm', 'mci.getAlarm returns: ', alarm )

	return alarm
end


--
-- Called when an file system monitor events arrive
--
mci.inotifyEvent = Inotify.event

--
-- Collector for every child process that finished in startup phase
--
function mci.collector
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
function mci.overflow
( )
	log( 'Normal', '--- OVERFLOW in event queue ---' )

	lsyncdStatus = 'fade'
end

--
-- Called by core on a hup signal.
--
function mci.hup
( )
	log( 'Normal', '--- HUP signal, resetting ---' )

	lsyncdStatus = 'fade'
end

--
-- Called by core on a term signal.
--
function mci.term
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

