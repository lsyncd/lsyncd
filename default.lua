--============================================================================
-- default.lua   Live (Mirror) Syncing Demon
--
-- The default table for the user to access.
-- This default layer 1 functions provide the higher layer functionality.
--
-- License: GPLv2 (see COPYING) or any later version
-- Authors: Axel Kittenberger <axkibe@gmail.com>
--============================================================================

if default
then
	error( 'default already loaded' )
end

default = { }


--
-- Only this items are inherited from the default
-- table
--
default._merge = {
	action          = true,
	checkgauge      = true,
	collect         = true,
	delay           = true,
	init            = true,
	maxDelays       = true,
	maxProcesses    = true,
	prepare         = true,
}

--
-- used to ensure there aren't typos in the keys
--
default.checkgauge = {
	action        =  true,
	checkgauge    =  true,
	collect       =  true,
	delay         =  true,
	exitcodes     =  true,
	init          =  true,
	maxDelays     =  true,
	maxProcesses  =  true,
	onAttrib      =  true,
	onCreate      =  true,
	onModify      =  true,
	onDelete      =  true,
	onStartup     =  true,
	onMove        =  true,
	prepare       =  true,
	source        =  true,
	target        =  true,
}

--
-- On default action the user's on*** scripts are called.
--
default.action = function
(
	inlet -- the inlet of the active sync.
)
	-- in case of moves getEvent returns the origin and dest of the move
	local event, event2 = inlet.getEvent( )

	local config = inlet.getConfig( )

	local func = config[ 'on'.. event.etype ]

	if type( func ) == 'function'
	then
		func( event, event2 )
	end

	-- if function didnt change the wait status its not interested
	-- in this event -> drop it.
	if event.status == 'wait'
	then
		inlet.discardEvent( event )
	end

end


--
-- Default collector.
--
-- Called when collecting a finished child process
--
default.collect = function
(
	agent,    -- event or event list being collected
	exitcode  -- the exitcode of the spawned process
)
	local config = agent.config

	local rc

	if config.exitcodes
	then
		rc = config.exitcodes[ exitcode ]
	elseif exitcode == 0
	then
		rc = 'ok'
	else
		rc = 'die'
	end

	-- TODO synchronize with similar code before
	if not agent.isList and agent.etype == 'Init'
	then
		if rc == 'ok'
		then
			log(
				'Normal',
				'Startup of ',
				agent.source,
				' -> ',
				agent.target,
				' finished.'
			)

			return 'ok'
		elseif rc == 'again'
		then
			if settings( 'insist' )
			then
				log(
					'Normal',
					'Retrying startup of ',
					agent.source,
					' -> ',
					agent.target,
					': ',
					exitcode
				)

				return 'again'
			else
				log(
					'Error',
					'Temporary or permanent failure on startup of ',
					agent.source,
					' -> ',
					agent.target,
					'. Terminating since "insist" is not set.'
				)

				terminate( -1 )
			end
		elseif rc == 'die'
		then
			log(
				'Error',
				'Failure on startup of ',
				agent.source,
				' -> ',
				agent.target,
				'.'
			)

			terminate( -1 )
		else
			log(
				'Error',
				'Unknown exitcode "',
				exitcode,
				'" on startup of ',
				agent.source,
				' -> ',
				agent.target,
				'.'
			)
			return 'die'
		end
	end

	if agent.isList
	then
		if rc == 'ok'
		then
			log(
				'Normal',
				'Finished a list after exitcode: ',
				exitcode
			)
		elseif rc == 'again'
		then
			log(
				'Normal',
				'Retrying a list after exitcode = ',
				exitcode
			)
		elseif rc == 'die'
		then
			log(
				'Error',
				'Failure with a list with exitcode = ',
				exitcode
			)
		else
			log(
				'Error',
				'Unknown exitcode "',exitcode,'" with a list'
			)

			rc = 'die'
		end
	else
		if rc == 'ok'
		then
			log(
				'Normal',
				'Finished ',
				agent.etype,
				' on ',
				agent.sourcePath,
				' = ',
				exitcode
			)
		elseif rc == 'again'
		then
			log(
				'Normal',
				'Retrying ',
				agent.etype,
				' on ',
				agent.sourcePath,
				' = ',
				exitcode
			)
		elseif rc == 'die'
		then
			log(
				'Error',
				'Failure with ',
				agent.etype,
				' on ',
				agent.sourcePath,
				' = ',
				exitcode
			)
		else
			log(
				'Normal',
				'Unknown exitcode "',
				exitcode,
				'" with ',
				agent.etype,
				' on ',
				agent.sourcePath,
				' = ',
				exitcode
			)

			rc = 'die'
		end
	end

	return rc
end


--
-- Called on the Init event sent
-- on (re)initialization of Lsyncd for every sync
--
default.init = function
(
	event -- the precreated init event.
)
	local config = event.config

	local inlet = event.inlet

	-- user functions
	-- calls a startup if given by user script.
	if type( config.onStartup ) == 'function'
	then
		config.onStartup( event )
		-- TODO honor some return codes of startup like "warmstart".
	end

	if event.status == 'wait'
	then
		-- user script did not spawn anything
		-- thus the blanket event is deleted again.
		inlet.discardEvent( event )
	end
end


--
-- The collapsor tries not to have more than these delays.
-- So the delay queue does not grow too large
-- since calculation for stacking events is n*log( n ) (or so)
--
default.maxDelays = 1000


--
-- The maximum number of processes Lsyncd will
-- simultanously spawn for this sync.
--
default.maxProcesses = 1


--
-- Exitcodes of rsync and what to do.
-- TODO move to rsync
--
default.rsyncExitCodes = {

	--
	-- if another config provides the same table
	-- this will not be inherited (merged) into that one
	--
	-- if it does not, integer keys are to be copied
	-- verbatim
	--
	_merge  = false,
	_verbatim = true,

	[   0 ] = 'ok',
	[   1 ] = 'die',
	[   2 ] = 'die',
	[   3 ] = 'again',
	[   4 ] = 'die',
	[   5 ] = 'again',
	[   6 ] = 'again',
	[  10 ] = 'again',
	[  11 ] = 'again',
	[  12 ] = 'again',
	[  14 ] = 'again',
	[  20 ] = 'again',
	[  21 ] = 'again',
	[  22 ] = 'again',

	-- partial transfers are ok, since Lsyncd has registered the event that
	-- caused the transfer to be partial and will recall rsync.
	[  23 ] = 'ok',
	[  24 ] = 'ok',

	[  25 ] = 'die',
	[  30 ] = 'again',
	[  35 ] = 'again',

	[ 255 ] = 'again',
}


--
-- Exitcodes of ssh and what to do.
--
default.sshExitCodes =
{
	--
	-- if another config provides the same table
	-- this will not be inherited (merged) into that one
	--
	-- if it does not, integer keys are to be copied
	-- verbatim
	--
	_merge = false,
	_verbatim = true,

	[   0 ] = 'ok',
	[ 255 ] = 'again',
}


--
-- Minimum seconds between two writes of the status file.
--
default.statusInterval = 10


--
-- Checks all keys to be in the checkgauge.
--
local function check
(
	config,
	gauge,
	subtable,
	level
)
	for k, v in pairs( config )
	do
		if not gauge[k]
		then
			error(
				'Parameter "'
				.. subtable
				.. k
				.. '" unknown.'
				.. ' ( if this is not a typo add it to checkgauge )',
				level
			);
		end

		if type( gauge [ k ] ) == 'table'
		then
			if type( v ) ~= 'table'
			then
				error(
					'Parameter "'
					.. subtable
					.. k
					.. '" must be a table.',
					level
				)
			end

			check(
				config[ k ],
				gauge[ k ],
				subtable .. k .. '.',
				level + 1
			)
		end
	end
end


default.prepare = function
(
	config, -- the config to prepare for
	level   -- current callback level for error reporting
)

	local gauge = config.checkgauge

	if not gauge
	then
		return
	end

	check( config, gauge, '', level + 1 )
end

