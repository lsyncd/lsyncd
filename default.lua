--============================================================================
-- default.lua   Live (Mirror) Syncing Demon
--
-- The default table for the user to access.
-- This default layer 1 functions provide the higher layer functionality.
--
-- License: GPLv2 (see COPYING) or any later version
-- Authors: Axel Kittenberger <axkibe@gmail.com>
--============================================================================

if default then error('default already loaded'); end

default = {
	-----
	-- Default action calls user scripts on**** functions.
	--
	action = function(inlet)
		-- in case of moves getEvent returns the origin and dest of the move
		local event, event2 = inlet.getEvent()
		local config = inlet.getConfig()
		local func = config['on'.. event.etype]
		if func then
			func(event, event2)
		end
		-- if function didnt change the wait status its not interested
		-- in this event -> drop it.
		if event.status == 'wait' then
			inlet.discardEvent(event)
		end
	end,


	-----
	-- Default collector.
	--
	-- Called when collecting a finished child process
	--
	collect = function(agent, exitcode)
		local config = agent.config
		local rc
		if config.exitcodes then
			rc = config.exitcodes[exitcode]
		elseif exitcode == 0 then
			rc = 'ok'
		else
			rc = 'die'
		end

		-- TODO synchronize with similar code before
		if not agent.isList and agent.etype == 'Init' then
			if rc == 'ok' then
				log('Normal', 'Startup of "',agent.source,'" finished.')
				return 'ok'
			elseif rc == 'again' then
				if settings.insist then
					log(
						'Normal',
						'Retrying startup of "',
						agent.source,
						'": ',
						exitcode
					)

					return 'again'
				else
					log(
						'Error',
						'Temporary or permanent failure on startup of "',
						agent.source,
						'". Terminating since "insist" is not set.'
					)
					terminate(-1) -- ERRNO
				end
			elseif rc == 'die' then
				log(
					'Error',
					'Failure on startup of "',
					agent.source,
					'".'
				)
				terminate(-1) -- ERRNO
			else
				log(
					'Error',
					'Unknown exitcode "',
					exitcode,
					'" on startup of "',
					agent.source,
					'".'
				)
				return 'die'
			end
		end

		if agent.isList then
			if rc == 'ok' then
				log('Normal', 'Finished a list = ',exitcode)
			elseif rc == 'again' then
				log('Normal', 'Retrying a list on exitcode = ',exitcode)
			elseif rc == 'die' then
				log('Error', 'Failure with a list on exitcode = ',exitcode)
			else
				log('Error', 'Unknown exitcode "',exitcode,'" with a list')
				rc = 'die'
			end
		else
			if rc == 'ok' then
				log('Normal', 'Retrying ',agent.etype,' on ',agent.sourcePath,' = ',exitcode)
			elseif rc == 'again' then
				log('Normal', 'Finished ',agent.etype,' on ',agent.sourcePath,' = ',exitcode)
			elseif rc == 'die' then
				log('Error', 'Failure with ',agent.etype,' on ',agent.sourcePath,' = ',exitcode)
			else
				log('Normal', 'Unknown exitcode "',exitcode,'" with ', agent.etype,
					' on ',agent.sourcePath,' = ',exitcode)
				rc = 'die'
			end
		end

		return rc
	end,

	-----
	-- called on (re)initialization of Lsyncd.
	--
	init = function(event)
		local config = event.config
		local inlet = event.inlet
		-- user functions
		-- calls a startup if given by user script.
		if type(config.onStartup) == 'function' then
			local startup = config.onStartup(event)
			-- TODO honor some return codes of startup like "warmstart".
		end

		if event.status == 'wait' then
			-- user script did not spawn anything
			-- thus the blanket event is deleted again.
			inlet.discardEvent(event)
		end
	end,

	-----
	-- The maximum number of processes Lsyncd will spawn simultanously for
	-- one sync.
	--
	maxProcesses = 1,

	-----
	-- Try not to have more than these delays.
	-- not too large, since total calculation for stacking
	-- events is n*log(n) or so..
	--
	maxDelays = 1000,

	-----
	-- a default configuration using /bin/cp|rm|mv.
	--
	direct = default_direct,

	------
	-- Exitcodes of rsync and what to do.
	--
	rsyncExitCodes = {
		[  0] = 'ok',
		[  1] = 'die',
		[  2] = 'die',
		[  3] = 'again',
		[  4] = 'die',
		[  5] = 'again',
		[  6] = 'again',
		[ 10] = 'again',
		[ 11] = 'again',
		[ 12] = 'again',
		[ 14] = 'again',
		[ 20] = 'again',
		[ 21] = 'again',
		[ 22] = 'again',
		[ 23] = 'ok', -- partial transfers are ok, since Lsyncd has registered the event that
		[ 24] = 'ok', -- caused the transfer to be partial and will recall rsync.
		[ 25] = 'die',
		[ 30] = 'again',
		[ 35] = 'again',
		[255] = 'again',
	},

	-----
	-- Exitcodes of ssh and what to do.
	--
	sshExitCodes = {
		[0]   = 'ok',
		[255] = 'again',
	},

	-----
	-- Minimum seconds between two writes of a status file.
	--
	statusInterval = 10,
}
