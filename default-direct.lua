--~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
-- default-direct.lua
--
--    Keeps two directories with /bin/cp, /bin/rm and /bin/mv in sync.
--    Startup still uses rsync tough.
--
--    A (Layer 1) configuration.
--
-- Note:
--    this is infact just a configuration using Layer 1 configuration
--    like any other. It only gets compiled into the binary by default.
--
--    You can simply use a modified one, by copying everything into a
--    config file of yours and name it differently.
--
-- License: GPLv2 (see COPYING) or any later version
-- Authors: Axel Kittenberger <axkibe@gmail.com>
--
--~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

if not default then
	error('default not loaded')
end

if not default.rsync then
	error('default-direct (currently) needs default.rsync loaded')
end

if default.direct then
	error('default-direct already loaded')
end

local direct = { }

default.direct = direct


--
-- known configuration parameters
--
direct.checkgauge = {
	--
	-- inherits rsync config params
	--
	default.rsync.checkgauge,

	rsyncExitCodes  =  true,
	onMove          =  true,
}


--
-- Spawns rsync for a list of events
--
direct.action = function(inlet)
	-- gets all events ready for syncing
	local event, event2 = inlet.getEvent()
	local config = inlet.getConfig()

	if event.etype == 'Create' then
		if event.isdir then
			spawn(
				event,
				'/bin/mkdir',
				'--',
				event.targetPath
			)
		else
			-- 'cp -t', not supported on OSX
			spawn(
				event,
				'/bin/cp',
				'-p',
				'--',
				event.sourcePath,
				event.targetPathdir
			)
		end
	elseif event.etype == 'Modify' then
		if event.isdir then
			error("Do not know how to handle 'Modify' on dirs")
		end
		spawn(event,
			'/bin/cp',
			'-p',
			'--',
			event.sourcePath,
			event.targetPathdir
		)
	elseif event.etype == 'Delete' then

		if
			config.delete ~= true and
			config.delete ~= 'running'
		then
			inlet.discardEvent(event)
			return
		end

		local tp = event.targetPath

		-- extra security check
		if tp == '' or tp == '/' or not tp then
			error('Refusing to erase your harddisk!')
		end

		spawn(event, '/bin/rm', '-rf', '--', tp)

	elseif event.etype == 'Move' then
		local tp = event.targetPath

		-- extra security check
		if tp == '' or tp == '/' or not tp then
			error('Refusing to erase your harddisk!')
		end

		local command = '/bin/mv -- "$1" "$2" || /bin/rm -rf -- "$1"'

		if
			config.delete ~= true and
			config.delete ~= 'running'
		then
			command = '/bin/mv -- "$1" "$2"'
		end

		spawnShell(
			event,
			command,
			event.targetPath,
			event2.targetPath
		)

	else
		log('Warn', 'ignored an event of type "',event.etype, '"')
		inlet.discardEvent(event)
	end
end

--
-- Called when collecting a finished child process
--
direct.collect = function(agent, exitcode)

	local config = agent.config

	if not agent.isList and agent.etype == 'Init' then
		local rc = config.rsyncExitCodes[exitcode]
		if rc == 'ok' then
			log('Normal', 'Startup of "',agent.source,'" finished: ', exitcode)
		elseif rc == 'again'
		then
			if settings( 'insist' )
			then
				log('Normal', 'Retrying startup of "',agent.source,'": ', exitcode)
			else
				log('Error', 'Temporary or permanent failure on startup of "',
				agent.source, '". Terminating since "insist" is not set.');
				terminate(-1) -- ERRNO
			end
		elseif rc == 'die' then
			log('Error', 'Failure on startup of "',agent.source,'": ', exitcode)
		else
			log('Error', 'Unknown exitcode on startup of "', agent.source,': "',exitcode)
			rc = 'die'
		end
		return rc
	end

	-- everything else is just as it is,
	-- there is no network to retry something.
	return
end

--
-- Spawns the recursive startup sync
-- (currently) identical to default rsync.
--
direct.init = default.rsync.init

--
-- Checks the configuration.
--
direct.prepare = function( config, level )

	default.rsync.prepare( config, level + 1 )

end

--
-- Default delay is very short.
--
direct.delay = 1

--
-- Let the core not split move events.
--
direct.onMove = true

--
-- Rsync configuration for startup.
--
direct.rsync = default.rsync.rsync
direct.rsyncExitCodes = default.rsyncExitCodes

--
-- By default do deletes.
--
direct.delete = true

--
-- On many system multiple disk operations just rather slow down
-- than speed up.

direct.maxProcesses = 1
