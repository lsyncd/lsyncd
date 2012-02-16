--~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
-- default-rsync.lua
--
--    Syncs with rsync ("classic" Lsyncd)
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

if not default   then error('default not loaded'); end
if default.rsync then error('default-rsync already loaded'); end

default.rsync = {
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

		local paths = elist.getPaths(
			function(etype, path1, path2)
				if string.byte(path1, -1) == 47 and
					(etype == 'Delete' or etype == 'Create')
				then
					return sub(path1)..'***', sub(path2)
				else
					return sub(path1), sub(path2)
				end
			end)
		-- stores all filters with integer index
		-- local filterI = inlet.getExcludes();
		local filterI = {}
		-- stores all filters with path index
		local filterP = {}

		-- adds one entry into the filter
		-- @param path ... path to add
		-- @param leaf ... true if this the original path
		--                 false if its a parent
		local function addToFilter(path)
			if filterP[path] then
				return
			end
			filterP[path]=true
			table.insert(filterI, path)
		end

		-- adds a path to the filter, for rsync this needs
		-- to have entries for all steps in the path, so the file
		-- d1/d2/d3/f1 needs filters
		-- 'd1/', 'd1/d2/', 'd1/d2/d3/' and 'd1/d2/d3/f1'
		for _, path in ipairs(paths) do
			if path and path ~= '' then
				addToFilter(path)
				local pp = string.match(path, '^(.*/)[^/]+/?')
				while pp do
					addToFilter(pp)
					pp = string.match(pp, '^(.*/)[^/]+/?')
				end
			end
		end

		local filterS = table.concat(filterI, '\n')
		local filter0 = table.concat(filterI, '\000')
		log('Normal', 'Calling rsync with filter-list of new/modified files/dirs\n', filterS)
		local config = inlet.getConfig()
		local delete = nil
		if config.delete then delete = { '--delete', '--ignore-errors' }; end
		spawn(elist, config.rsyncBinary,
			'<', filter0,
			config.rsyncOpts,
			'-r',
			delete,
			'--force',
			'--from0',
			'--include-from=-',
			'--exclude=*',
			config.source,
			config.target)
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

		if #excludes == 0 then
			log('Normal', 'recursive startup rsync: ', config.source, ' -> ', config.target)
			spawn(event, config.rsyncBinary,
				delete,
				config.rsyncOpts,
				'-r',
				config.source,
				config.target)
		else
			local exS = table.concat(excludes, '\n')
			log('Normal', 'recursive startup rsync: ',config.source,
				' -> ',config.target,' excluding\n',exS)
			spawn(event, config.rsyncBinary,
				'<', exS,
				'--exclude-from=-',
				delete,
				config.rsyncOpts,
				'-r',
				config.source,
				config.target)
		end
	end,

	-----
	-- Checks the configuration.
	--
	prepare = function(config)
		if not config.target then
			error('default.rsync needs "target" configured', 4)
		end

		if config.rsyncOps then
			error('did you mean rsyncOpts with "t"?', 4)
		end

		-- appends a / to target if not present
		if string.sub(config.target, -1) ~= '/' then
			config.target = config.target..'/'
		end
	end,

	-----
	-- rsync uses default collect
	----

	-----
	-- By default do deletes.
	--
	delete = true,

	-----
	-- The rsync binary to be called.
	--
	rsyncBinary = '/usr/bin/rsync',

	-----
	-- Calls rsync with this default short opts.
	--
	rsyncOpts = '-lts',

	-----
	-- Exit codes for rsync.
	--
	exitcodes = default.rsyncExitCodes,

	-----
	-- Default delay
	--
	delay = 15,
}
