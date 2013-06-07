--
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
--

if not default then
	error( 'default not loaded' );
end

if not default.rsync then
	error( 'default.rsync not loaded' );
end

if default.rsyncssh then
	error( 'default-rsyncssh already loaded' );
end

--
-- rsyncssh extends default.rsync
--
local rsyncssh = { default.rsync }
default.rsyncssh = rsyncssh

--
-- used to ensure there aren't typos in the keys
--
rsyncssh.checkgauge = {

	-- unsets the inherited value of from default.rsync
	target          =  false,
	onMove          =  true,

	-- rsyncssh users host and targetdir
	host            =  true,
	targetdir       =  true,
	sshExitCodes    =  true,
	rsyncExitCodes  =  true,

	-- ssh settings
	ssh = {
		binary      =  true,
		port        =  true,
		_extra      =  true
	},

	-- xargs settings
	xargs = {
		binary      =  true,
		delimiter   =  true,
		_extra      =  true
	}
}

--
-- Spawns rsync for a list of events
--
rsyncssh.action = function( inlet )

	local event, event2 = inlet.getEvent()
	local config = inlet.getConfig()

	-- makes move local on target host
	-- if the move fails, it deletes the source
	if event.etype == 'Move' then
		log('Normal', 'Moving ',event.path,' -> ',event2.path)

		spawn(
			event,
			config.ssh.binary,
			config.ssh._computed,
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

		if
			config.delete ~= true and
			config.delete ~= 'running'
		then
			inlet.discardEvent(event)
			return
		end

		-- gets all other deletes ready to be
		-- executed
		local elist = inlet.getEvents(
			function( e )
				return e.etype == 'Delete'
			end
		)

		-- returns the paths of the delete list
		local paths = elist.getPaths(
			function( etype, path1, path2 )
				if path2 then
					return config.targetdir..path1, config.targetdir..path2
				else
					return config.targetdir..path1
				end
			end
		)

		-- ensures none of the paths is '/'
		for _, v in pairs( paths ) do
			if string.match(v, '^%s*/+%s*$') then
				log('Error', 'refusing to `rm -rf /` the target!')
				terminate(-1) -- ERRNO
			end
		end

		log(
			'Normal',
			'Deleting list\n',
			table.concat( paths, '\n' )
		)

		local params = { }

		spawn(
			elist,
			config.ssh.binary,
			'<', table.concat(paths, config.xargs.delimiter),
			params,
			config.ssh._computed,
			config.host,
			config.xargs.binary,
			config.xargs._extra
		)

		return
	end

	--
	-- for everything else a rsync is spawned
	--
	local elist = inlet.getEvents(
		function(e)
			-- TODO use a table
			return e.etype ~= 'Move' and
			       e.etype ~= 'Delete' and
				   e.etype ~= 'Init' and
				   e.etype ~= 'Blanket'
		end
	)

	local paths = elist.getPaths( )

	--
	-- removes trailing slashes from dirs.
	--
	for k, v in ipairs( paths ) do
		if string.byte(v, -1) == 47 then
			paths[k] = string.sub(v, 1, -2)
		end

	end

	local sPaths = table.concat(paths, '\n')
	local zPaths = table.concat(paths, '\000')

	log('Normal', 'Rsyncing list\n', sPaths)

	spawn(
		elist,
		config.rsync.binary,
		'<', zPaths,
		config.rsync._computed,
		'--from0',
		'--files-from=-',
		config.source,
		config.host .. ':' .. config.targetdir
	)
end

-----
-- Called when collecting a finished child process
--
rsyncssh.collect = function( agent, exitcode )

	local config = agent.config

	if not agent.isList and agent.etype == 'Init' then
		local rc = config.rsyncExitCodes[exitcode]

		if rc == 'ok' then
			log('Normal', 'Startup of "',agent.source,'" finished: ', exitcode)
		elseif rc == 'again' then
			if settings('insist') then
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
		if rc == 'ok' then
			log('Normal', 'Finished (list): ',exitcode)
		elseif rc == 'again' then
			log('Normal', 'Retrying (list): ',exitcode)
		elseif rc == 'die' then
			log('Error',  'Failure (list): ', exitcode)
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

end

--
-- checks the configuration.
--
rsyncssh.prepare = function( config, level )

	default.rsync.prepare( config, level + 1, true )

	if not config.host then
		error(
			'default.rsyncssh needs "host" configured',
			level
		)
	end

	if not config.targetdir then
		error(
			'default.rsyncssh needs "targetdir" configured',
			level
		)
	end

	--
	-- computes the ssh options
	--
	if config.ssh._computed then
		error(
			'please do not use the internal rsync._computed parameter',
			level
		)
	end

	local cssh = config.ssh;
	cssh._computed = { }
	local computed = cssh._computed
	local computedN = 1

	if cssh._extra then
		for k, v in ipairs( cssh._extra ) do
			computed[ computedN ] = v
			computedN = computedN  + 1
		end
	end

	if cssh.port then
		computed[ computedN     ] = '-p'
		computed[ computedN + 1 ] = cssh.port
		computedN = computedN + 2

		local rsyncc = config.rsync._computed
		rsyncc[ #rsyncc + 1 ] = '--rsh=ssh -p ' .. cssh.port
	end

	-- appends a slash to the targetdir if missing
	if string.sub( config.targetdir, -1 ) ~= '/' then
		config.targetdir = config.targetdir .. '/'
	end

end

--
-- allow processes
--
rsyncssh.maxProcesses = 1

--
-- The core should not split move events
--
rsyncssh.onMove = true

--
-- default delay
--
rsyncssh.delay = 15


--
-- no default exit codes
--
rsyncssh.exitcodes = false

--
-- rsync exit codes
--
rsyncssh.rsyncExitCodes = default.rsyncExitCodes

--
-- ssh exit codes
--
rsyncssh.sshExitCodes = default.sshExitCodes

--
-- xargs calls configuration
--
-- xargs is used to delete multiple remote files, when ssh access is
-- available this is simpler than to build filters for rsync for this.
--
rsyncssh.xargs = {

	--
	-- the binary called (on target host)
	binary = '/usr/bin/xargs',

	--
	-- delimiter, uses null by default, you might want to override this for older
	-- by for example '\n'
	delimiter = '\000',

	--
	-- extra parameters
	_extra = { '-0', 'rm -rf' }
}

--
-- ssh calls configuration
--
-- ssh is used to move and delete files on the target host
--
rsyncssh.ssh = {

	--
	-- the binary called
	--
	binary = '/usr/bin/ssh',

	--
	-- if set connect to this port
	--
	port = nil,

	--
	-- extra parameters
	--
	_extra = { }
}

