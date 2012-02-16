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
--    You can simply use a modified one, by copying everything into a
--    config file of yours and name it differently.
--
-- License: GPLv2 (see COPYING) or any later version
-- Authors: Axel Kittenberger <axkibe@gmail.com>
--
--~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

if not default       then error('default not loaded'); end
if not default.rsync then error('default-direct (currently) needs default.rsync loaded'); end
if default.direct    then error('default-direct already loaded'); end

default.direct = {
	-----
	-- Spawns rsync for a list of events
	--
	action = function(inlet)
		-- gets all events ready for syncing
		local event, event2 = inlet.getEvent()
		local config = inlet.getConfig()

		if event.etype == 'Create' then
			if event.isdir then
				spawn(
					event,
					'/bin/mkdir',
					event.targetPath
				)
			else
				spawn(
					event,
					'/bin/cp',
					event.sourcePath,
					-- '-t', not supported on OSX
					event.targetPathdir
				)
			end
		elseif event.etype == 'Modify' then
			if event.isdir then
				error("Do not know how to handle 'Modify' on dirs")
			end
			spawn(event,
				'/bin/cp',
				'-t',
				event.targetPathdir,
				event.sourcePath
			)
		elseif event.etype == 'Delete' then
			if not config.delete then
				inlet.discardEvent(event)
			end

			local tp = event.targetPath
			-- extra security check
			if tp == '' or tp == '/' or not tp then
				error('Refusing to erase your harddisk!')
			end
			spawn(event, '/bin/rm', '-rf', tp)
		elseif event.etype == 'Move' then
			local tp = event.targetPath
			-- extra security check
			if tp == '' or tp == '/' or not tp then
				error('Refusing to erase your harddisk!')
			end
			local command = '/bin/mv $1 $2 || /bin/rm -rf $1'
			if not config.delete then command = '/bin/mv $1 $2'; end
			spawnShell(
				event,
				command,
				event.targetPath,
				event2.targetPath)
		else
			log('Warn', 'ignored an event of type "',event.etype, '"')
			inlet.discardEvent(event)
		end
	end,

	-----
	-- Called when collecting a finished child process
	--
	collect = function(agent, exitcode)
		local config = agent.config

		if not agent.isList and agent.etype == 'Init' then
			local rc = config.rsyncExitCodes[exitcode]
			if rc == 'ok' then
				log('Normal', 'Startup of "',agent.source,'" finished: ', exitcode)
			elseif rc == 'again' then
				if settings.insist then
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
	end,

	-----
	-- Spawns the recursive startup sync
	-- (currently) identical to default rsync.
	--
	init = default.rsync.init,

	-----
	-- Checks the configuration.
	--
	prepare = function(config)
		if not config.target then
			error('default.direct needs "target".', 4)
		end

		if config.rsyncOps then
			error('did you mean rsyncOpts with "t"?', 4)
		end
	end,

	-----
	-- Default delay is very short.
	--
	delay = 1,

	------
	-- Let the core not split move events.
	--
	onMove = true,

	-----
	-- The rsync binary called.
	--
	rsyncBinary = '/usr/bin/rsync',

	-----
	-- For startup sync
	--
	rsyncOpts = '-lts',

	-----
	-- By default do deletes.
	--
	delete = true,

	-----
	-- rsync exit codes
	--
	rsyncExitCodes = default.rsyncExitCodes,

	-----
	-- On many system multiple disk operations just rather slow down
	-- than speed up.

	maxProcesses = 1,
}
