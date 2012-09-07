--~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
-- User configuration file for lsyncd.
--
--    Syncs with 'lftp'.
--
--~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

lftp = {

	-----
	-- Spawns rsync for a list of events
	--
	action = function(inlet)

		-- gets all events ready for syncing
		local elist = inlet.getEvents(
			function(event)
				return event.etype ~= 'Init' and event.etype ~= 'Blanket'
			end
		)

		-----
		-- replaces filter rule by literals
		--
		local function sub(p)
			if not p then
				return
			end
			return p:gsub('%?', '\\?'):
			         gsub('%*', '\\*'):
			         gsub('%[', '\\['):
			         gsub('%]', '\\]')
		end

		local config = inlet.getConfig()

		local commands = elist.getPaths(
			function(etype, path1, path2)
				if etype == 'Delete' then
					if string.byte(path1, -1) == 47 then
						return 'rm -r '..
							config.targetdir..sub(path1)
					else
						return 'rm '..
							config.targetdir..sub(path1)
					end
				elseif
					etype == 'Create' or
					etype == 'Modify' or
		 			etype == 'Attrib'
		 		then
					if string.byte(path1, -1) == 47 then
						return 'mirror -R '..
							config.source..sub(path1)..' '..
							config.targetdir..sub(path1)
					else
						return 'put '..
							config.source..sub(path1)..
							' -o '..config.targetdir..sub(path1)
					end
				end
			end
		)

		if #commands == 0 then
			spawn(elist, '/bin/true')
			return
		end

		commands = table.concat(commands, ';\n')

		log('Normal', 'Calling lftp with commands\n', commands)

		spawn(elist, '/usr/bin/lftp',
			'<', commands,
			'-u', config.user..','..config.pass, config.host
		)
	end,

	-----
	-- Spawns the recursive startup sync
	--
	init = function(event)
		local config = event.config
		local inlet = event.inlet
		local excludes = inlet.getExcludes()
		local delete = nil
		if config.delete then delete = { '--delete', '--ignore-errors' }; end

		if #excludes ~= 0 then
			error('lftp does not work with excludes', 4)
		end

		log('Normal', 'recursive startup lftp: ', config.source, ' to host: ', config.host)

		spawn(event, '/usr/bin/lftp',
			'-c',
			'open -u '..config.user..','..config.pass..' '..config.host..'; '..
			'mirror -R -e '..config.source..' '..config.targetdir..';'
		)
	end,

	-----
	-- Checks the configuration.
	--
	prepare = function(config)

		if not config.host then
			error('lftps needs "host" configured', 4);
		end

		if not config.user then
			error('lftps needs "user" configured', 4);
		end

		if not config.pass then
			error('lftps needs "pass" configured', 4);
		end

		if not config.targetdir then
			error('lftp needs "targetdir" configured', 4)
		end

		if config.target then
			error('lftp needs NOT "target" configured', 4)
		end

		if config.exclude then
			error('lftp does not work with excludes', 4)
		end

		if config.rsyncOpts then
			error('lftp needs NOT "rsyncOpts" configured', 4)
		end

		if string.sub(config.targetdir, -1) == '/' then
			error('please make targetdir not end with a /', 4)
		end

	end,

	-----
	-- Exit codes for rsync.
	--
	exitcodes = {
		[  0] = 'ok',
		[  1] = 'ok',
	},

	-----
	-- Default delay
	--
	delay = 1,
}

sync{
	lftp,
	host      = 'localhost',
	user      = 'test',
	pass      = 'test',
	source    = 'src',
	targetdir = '.',
}
