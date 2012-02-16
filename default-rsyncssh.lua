--~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
-- default-rsyncssh.lua
--
--    Improved rsync - sync with rsync, but moves and deletes executed over ssh.
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

if not default      then error('default not loaded'); end
if default.rsyncssh then error('default-rsyncssh already loaded'); end

default.rsyncssh = {
	-----
	-- Spawns rsync for a list of events
	--
	action = function(inlet)
		local event, event2 = inlet.getEvent()
		local config = inlet.getConfig()

		-- makes move local on host
		-- if fails deletes the source...
		if event.etype == 'Move' then
			log('Normal', 'Moving ',event.path,' -> ',event2.path)
			spawn(event, '/usr/bin/ssh',
				config.host,
				'mv',
				'\"' .. config.targetdir .. event.path .. '\"',
				'\"' .. config.targetdir .. event2.path .. '\"',
				'||', 'rm', '-rf',
				'\"' .. config.targetdir .. event.path .. '\"')
			return
		end

		-- uses ssh to delete files on remote host
		-- instead of constructing rsync filters
		if event.etype == 'Delete' then
			if not config.delete then
				inlet.discardEvent(event)
				return
			end

			local elist = inlet.getEvents(
				function(e)
					return e.etype == 'Delete'
				end)

			local paths = elist.getPaths(
				function(etype, path1, path2)
					if path2 then
						return config.targetdir..path1, config.targetdir..path2
					else
						return config.targetdir..path1
					end
				end)

			for _, v in pairs(paths) do
				if string.match(v, '^%s*/+%s*$') then
					log('Error', 'refusing to `rm -rf /` the target!')
					terminate(-1) -- ERRNO
				end
			end

			local sPaths = table.concat(paths, '\n')
			local zPaths = table.concat(paths, config.xargs.delimiter)
			log('Normal', 'Deleting list\n', sPaths)
			spawn(elist, '/usr/bin/ssh',
				'<', zPaths,
				config.host,
				config.xargs.binary, config.xargs.xparams)
			return
		end

		-- for everything else spawn a rsync
		local elist = inlet.getEvents(
			function(e)
				-- TODO use a table
				return e.etype ~= 'Move' and
				       e.etype ~= 'Delete' and
					   e.etype ~= 'Init' and
					   e.etype ~= 'Blanket'
			end)
		local paths = elist.getPaths()

		-- removes trailing slashes from dirs.
		for k, v in ipairs(paths) do
			if string.byte(v, -1) == 47 then
				paths[k] = string.sub(v, 1, -2)
			end
		end
		local sPaths = table.concat(paths, '\n')
		local zPaths = table.concat(paths, '\000')
		log('Normal', 'Rsyncing list\n', sPaths)
		spawn(
			elist, config.rsyncBinary,
			'<', zPaths,
			config.rsyncOpts,
			'--from0',
			'--files-from=-',
			config.source,
			config.host .. ':' .. config.targetdir
		)
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

		if agent.isList then
			local rc = config.rsyncExitCodes[exitcode]
			if     rc == 'ok'    then log('Normal', 'Finished (list): ',exitcode)
			elseif rc == 'again' then log('Normal', 'Retrying (list): ',exitcode)
			elseif rc == 'die'   then log('Error',  'Failure (list): ', exitcode)
			else
				log('Error', 'Unknown exitcode (list): ',exitcode)
				rc = 'die'
			end
			return rc
		else
			local rc = config.sshExitCodes[exitcode]
			if rc == 'ok' then
				log('Normal', 'Finished ',agent.etype,' ',agent.sourcePath,': ',exitcode)
			elseif rc == 'again' then
				log('Normal', 'Retrying ',agent.etype,' ',agent.sourcePath,': ',exitcode)
			elseif rc == 'die' then
				log('Normal', 'Failure ',agent.etype,' ',agent.sourcePath,': ',exitcode)
			else
				log('Error', 'Unknown exitcode ',agent.etype,' ',agent.sourcePath,': ',exitcode)
				rc = 'die'
			end
			return rc
		end
	end,

	-----
	-- Spawns the recursive startup sync
	--
	init = function(event)
		local config = event.config
		local inlet = event.inlet
		local excludes = inlet.getExcludes()
		local target = config.host .. ':' .. config.targetdir
		local delete = nil
		if config.delete then delete = { '--delete', '--ignore-errors' }; end

		if #excludes == 0 then
			log('Normal', 'Recursive startup rsync: ',config.source,' -> ',target)
			spawn(
				event, config.rsyncBinary,
				delete,
				'-r',
				config.rsyncOpts,
				config.source,
				target
			)
		else
			local exS = table.concat(excludes, '\n')
			log('Normal', 'Recursive startup rsync: ',config.source,
				' -> ',target,' with excludes.')
			spawn(
				event, config.rsyncBinary,
				'<', exS,
				'--exclude-from=-',
				delete,
				'-r',
				config.rsyncOpts,
				config.source,
				target
			)
		end
	end,

	-----
	-- Checks the configuration.
	--
	prepare = function(config)
		if not config.host      then error('default.rsyncssh needs "host" configured', 4) end
		if not config.targetdir then error('default.rsyncssh needs "targetdir" configured', 4) end

		if config.rsyncOps then
			error('did you mean rsyncOpts with "t"?', 4)
		end

		-- appends a slash to the targetdir if missing
		if string.sub(config.targetdir, -1) ~= '/' then
			config.targetdir = config.targetdir .. '/'
		end
	end,

	-----
	-- The rsync binary called.
	--
	rsyncBinary = '/usr/bin/rsync',

	-----
	-- Calls rsync with this default short opts.
	--
	rsyncOpts = '-lts',

	-----
	-- allow processes
	--
	maxProcesses = 1,

	------
	-- Let the core not split move events.
	--
	onMove = true,

	-----
	-- Default delay.
	--
	delay = 15,


	-----
	-- By default do deletes.
	--
	delete = true,

	-----
	-- rsync exit codes
	--
	rsyncExitCodes = default.rsyncExitCodes,

	-----
	-- ssh exit codes
	--
	sshExitCodes = default.sshExitCodes,

	-----
	-- Delimiter, the binary and the paramters passed to xargs
	-- xargs is used to delete multiple remote files, when ssh access is
	-- available this is simpler than to build filters for rsync for this.
	-- Default uses '0' as limiter, you might override this for old systems.
	--
	xargs = {
		binary = '/usr/bin/xargs',
		delimiter = '\000',
		xparams = {'-0', 'rm -rf'}
	}
}
