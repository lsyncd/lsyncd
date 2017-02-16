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

if not default
then
	error( 'default not loaded' );
end

if not default.rsync
then
	error( 'default.rsync not loaded' );
end

if default.rsyncssh
then
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
		binary       =  true,
		identityFile =  true,
		options      =  true,
		port         =  true,
		_extra       =  true
	},
}


--
-- Returns true for non Init, Blanket and Move events.
--
local eventNotInitBlankMove =
	function
(
	event
)
	-- TODO use a table
	if event.etype == 'Move'
	or event.etype == 'Init'
	or event.etype == 'Blanket'
	then
		return 'break'
	else
		return true
	end
end


--
-- Replaces what rsync would consider filter rules by literals.
--
local replaceRsyncFilter =
	function
(
	path
)
	if not path
	then
		return
	end

	return(
		path
		:gsub( '%?', '\\?' )
		:gsub( '%*', '\\*' )
		:gsub( '%[', '\\[' )
	)
end


--
-- Spawns rsync for a list of events
--
rsyncssh.action = function
(
	inlet
)
	local config = inlet.getConfig( )

	local event, event2 = inlet.getEvent( )

	-- makes move local on target host
	-- if the move fails, it deletes the source
	if event.etype == 'Move'
	then
		local path1 = config.targetdir .. event.path

		local path2 = config.targetdir .. event2.path

		path1 = "'" .. path1:gsub ('\'', '\'"\'"\'') .. "'"
		path2 = "'" .. path2:gsub ('\'', '\'"\'"\'') .. "'"

		log(
			'Normal',
			'Moving ',
			event.path,
			' -> ',
			event2.path
		)

		spawn(
			event,
			config.ssh.binary,
			config.ssh._computed,
			config.host,
			'mv',
			path1,
			path2,
			'||', 'rm', '-rf',
			path1
		)

		return
	end

	-- otherwise a rsync is spawned
	local elist = inlet.getEvents( eventNotInitBlankMove )

	-- gets the list of paths for the event list
	-- deletes create multi match patterns
	local paths = elist.getPaths( )

	--
	-- Replaces what rsync would consider filter rules by literals
	--
	local function sub( p )
		if not p then
			return
		end

		return p:
			gsub( '%?', '\\?' ):
			gsub( '%*', '\\*' ):
			gsub( '%[', '\\[' ):
			gsub( '%]', '\\]' )
	end

	--
	-- Gets the list of paths for the event list
	--
	-- Deletes create multi match patterns
	--
	local paths = elist.getPaths(
		function( etype, path1, path2 )
			if string.byte( path1, -1 ) == 47 and etype == 'Delete' then
				return sub( path1 )..'***', sub( path2 )
			else
				return sub( path1 ), sub( path2 )
			end
		end
	)

	-- stores all filters by integer index
	local filterI = { }

	-- stores all filters with path index
	local filterP = { }

	-- adds one path to the filter
	local function addToFilter( path )

		if filterP[ path ]
		then
			return
		end

		filterP[ path ] = true

		table.insert( filterI, path )
	end

	-- adds a path to the filter.
	--
	-- rsync needs to have entries for all steps in the path,
	-- so the file for example d1/d2/d3/f1 needs following filters:
	-- 'd1/', 'd1/d2/', 'd1/d2/d3/' and 'd1/d2/d3/f1'
	for _, path in ipairs( paths )
	do
		if path and path ~= ''
		then
			addToFilter(path)

			local pp = string.match( path, '^(.*/)[^/]+/?' )

			while pp
			do
				addToFilter(pp)
				pp = string.match( pp, '^(.*/)[^/]+/?' )
			end

		end

	end

	log(
		'Normal',
		'Calling rsync with filter-list of new/modified files/dirs\n',
		table.concat( filterI, '\n' )
	)

	local config = inlet.getConfig( )

	local delete = nil

	if config.delete == true or config.delete == 'running'
	then
		delete = { '--delete', '--ignore-errors' }
	end

	spawn(
		elist,
		config.rsync.binary,
		'<', table.concat( filterI, '\000' ),
		config.rsync._computed,
		'-r',
		delete,
		'--force',
		'--from0',
		'--include-from=-',
		'--exclude=*',
		config.source,
		config.host .. ':' .. config.targetdir
	)
end


----
---- NOTE: This optimized version can be used once
----       https://bugzilla.samba.org/show_bug.cgi?id=12569
----       is fixed.
----
--
-- Spawns rsync for a list of events
--
--rsyncssh.action = function
--(
--	inlet
--)
--	local config = inlet.getConfig( )
--
--	local event, event2 = inlet.getEvent( )
--
--	-- makes move local on target host
--	-- if the move fails, it deletes the source
--	if event.etype == 'Move'
--	then
--		local path1 = config.targetdir .. event.path
--
--		local path2 = config.targetdir .. event2.path
--
--		path1 = "'" .. path1:gsub ('\'', '\'"\'"\'') .. "'"
--		path2 = "'" .. path2:gsub ('\'', '\'"\'"\'') .. "'"
--
--		log(
--			'Normal',
--			'Moving ',
--			event.path,
--			' -> ',
--			event2.path
--		)
--
--		spawn(
--			event,
--			config.ssh.binary,
--			config.ssh._computed,
--			config.host,
--			'mv',
--			path1,
--			path2,
--			'||', 'rm', '-rf',
--			path1
--		)
--
--		return
--	end
--
--	-- otherwise a rsync is spawned
--	local elist = inlet.getEvents( eventNotInitBlankMove )
--
--	-- gets the list of paths for the event list
--	-- deletes create multi match patterns
--	local paths = elist.getPaths( )
--
--	-- removes trailing slashes from dirs.
--	for k, v in ipairs( paths )
--	do
--		if string.byte( v, -1 ) == 47
--		then
--			paths[ k ] = string.sub( v, 1, -2 )
--		end
--	end
--
--	log(
--		'Normal',
--		'Rsyncing list\n',
--		table.concat( paths, '\n' )
--	)
--
--	local delete = nil
--
--	if config.delete == true
--	or config.delete == 'running'
--	then
--		delete = { '--delete-missing-args', '--ignore-errors' }
--	end
--
--	spawn(
--		elist,
--		config.rsync.binary,
--		'<', table.concat( paths, '\000' ),
--		config.rsync._computed,
--		delete,
--		'--force',
--		'--from0',
--		'--files-from=-',
--		config.source,
--		config.host .. ':' .. config.targetdir
--	)
--end


--
-- Called when collecting a finished child process
--
rsyncssh.collect = function
(
	agent,
	exitcode
)
	local config = agent.config

	if not agent.isList and agent.etype == 'Init'
	then
		local rc = config.rsyncExitCodes[exitcode]

		if rc == 'ok'
		then
			log('Normal', 'Startup of "', agent.source, '" finished: ', exitcode)
		elseif rc == 'again'
		then
			if settings('insist')
			then
				log( 'Normal', 'Retrying startup of "', agent.source, '": ', exitcode )
			else
				log(
					'Error',
					'Temporary or permanent failure on startup of "',
					agent.source, '". Terminating since "insist" is not set.' 
				)

				terminate( -1 ) -- ERRNO
			end
		elseif rc == 'die'
		then
			log( 'Error', 'Failure on startup of "',agent.source,'": ', exitcode )
		else
			log( 'Error', 'Unknown exitcode on startup of "', agent.source,': "',exitcode )

			rc = 'die'
		end

		return rc
	end

	if agent.isList
	then
		local rc = config.rsyncExitCodes[ exitcode ]

		if rc == 'ok'
		then
			log( 'Normal', 'Finished (list): ', exitcode )
		elseif rc == 'again'
		then
			log( 'Normal', 'Retrying (list): ', exitcode )
		elseif rc == 'die'
		then
			log( 'Error',  'Failure (list): ', exitcode )
		else
			log( 'Error', 'Unknown exitcode (list): ', exitcode )

			rc = 'die'
		end
		return rc
	else
		local rc = config.sshExitCodes[exitcode]

		if rc == 'ok'
		then
			log( 'Normal', 'Finished ', agent.etype,' ', agent.sourcePath, ': ', exitcode )
		elseif rc == 'again'
		then
			log( 'Normal', 'Retrying ', agent.etype, ' ', agent.sourcePath, ': ', exitcode )
		elseif rc == 'die'
		then
			log( 'Normal', 'Failure ', agent.etype, ' ', agent.sourcePath, ': ', exitcode )
		else
			log( 'Error', 'Unknown exitcode ',agent.etype,' ',agent.sourcePath,': ',exitcode )

			rc = 'die'
		end

		return rc
	end

end

--
-- checks the configuration.
--
rsyncssh.prepare = function
(
	config,
	level
)
	default.rsync.prepare( config, level + 1, true )

	if not config.host
	then
		error(
			'default.rsyncssh needs "host" configured',
			level
		)
	end

	if not config.targetdir
	then
		error(
			'default.rsyncssh needs "targetdir" configured',
			level
		)
	end

	--
	-- computes the ssh options
	--
	if config.ssh._computed
	then
		error(
			'please do not use the internal rsync._computed parameter',
			level
		)
	end

	if config.maxProcesses ~= 1
	then
		error(
			'default.rsyncssh must have maxProcesses set to 1.',
			level
		)
	end

	local cssh = config.ssh;

	cssh._computed = { }

	local computed = cssh._computed

	local computedN = 1

	local rsyncc = config.rsync._computed

	if cssh.identityFile
	then
		computed[ computedN ] = '-i'

		computed[ computedN + 1 ] = cssh.identityFile

		computedN = computedN + 2

		if not config.rsync._rshIndex
		then
			config.rsync._rshIndex = #rsyncc + 1

			rsyncc[ config.rsync._rshIndex ] = '--rsh=ssh'
		end

		rsyncc[ config.rsync._rshIndex ] =
			rsyncc[ config.rsync._rshIndex ] ..
			' -i ' ..
			cssh.identityFile
	end

	if cssh.options
	then
		for k, v in pairs( cssh.options )
		do
			computed[ computedN ] = '-o'

			computed[ computedN + 1 ] = k .. '=' .. v

			computedN = computedN + 2

			if not config.rsync._rshIndex
			then
				config.rsync._rshIndex = #rsyncc + 1

				rsyncc[ config.rsync._rshIndex ] = '--rsh=ssh'
			end

			rsyncc[ config.rsync._rshIndex ] =
				table.concat(
					{
						rsyncc[ config.rsync._rshIndex ],
						' -o ',
						k,
						'=',
						v
					},
					''
				)
		end
	end

	if cssh.port
	then
		computed[ computedN ] = '-p'

		computed[ computedN + 1 ] = cssh.port

		computedN = computedN + 2

		if not config.rsync._rshIndex
		then
			config.rsync._rshIndex = #rsyncc + 1

			rsyncc[ config.rsync._rshIndex ] = '--rsh=ssh'
		end

		rsyncc[ config.rsync._rshIndex ] =
			rsyncc[ config.rsync._rshIndex ] .. ' -p ' .. cssh.port
	end

	if cssh._extra
	then
		for k, v in ipairs( cssh._extra )
		do
			computed[ computedN ] = v

			computedN = computedN  + 1
		end
	end

	-- appends a slash to the targetdir if missing
	if string.sub( config.targetdir, -1 ) ~= '/'
	then
		config.targetdir =
			config.targetdir .. '/'
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
	-- if set adds this key to ssh
	--
	identityFile = nil,

	--
	-- if set adds this special options to ssh
	--
	options = nil,

	--
	-- if set connect to this port
	--
	port = nil,

	--
	-- extra parameters
	--
	_extra = { }
}

