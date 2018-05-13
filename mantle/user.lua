--
-- user.lua from Lsyncd -- the Live (Mirror) Syncing Demon
--
--
-- Lsyncd user script interface
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


user = { }


--
-- Main utility to create new observations.
--
-- Returns an Inlet to that sync.
--
user.sync =
	function
(
	opts
)
	-- Checks if user overwrote the settings function.
	-- ( was Lsyncd < 2.1 style )
	if userenv.settings ~= user.settings
	then
		log(
			'Error',
			'Do not use settings = { ... }\n'..
			'      please use settings{ ... } (without the equal sign)'
		)

		os.exit( -1 )
	end

	if lsyncdStatus ~= 'init'
	then
		error( 'Sync can only be created during initialization.', 2 )
	end

	return SyncMaster.add( opts ).inlet
end


--
-- Spawns a new child process.
--
user.spawn =
	function
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
user.spawnShell =
	function
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
user.observefd =
	function
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
user.nonobservefd =
	function
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
user.alarm = UserAlarms.alarm


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
-- The settings call
--
user.settings =
	function
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


--
-- Provides a way for user scripts to browse (and alter) active sync list.
--
user.syncs =
( function( )

	local mt =
	{
		__ipairs =
			function
		( )
			print( 'syncs.ipairs!!!' )
		end
	}

	-- public interface
	local intf = {
		add = sync
	}

	setmetatable( intf, mt )

	return intf
end ) ( )
