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


if not default then error( 'default not loaded' ) end

if default.rsync then error( 'default-rsync already loaded' ) end


local rsync = { }

default.rsync = rsync

-- uses default collect

--
-- used to ensure there aren't typos in the keys
--
rsync.checkgauge = {

	-- unsets default user action handlers
	onCreate    =  false,
	onModify    =  false,
	onDelete    =  false,
	onStartup   =  false,
	onMove      =  false,

	delete      =  true,
	exclude     =  true,
	excludeFrom =  true,
	filter      =  true,
	filterFrom  =  true,
	target      =  true,

	rsync  = {
		acls              =  true,
		append            =  true,
		append_verify     =  true,
		archive           =  true,
		backup            =  true,
		backup_dir        =  true,
		binary            =  true,
		bwlimit           =  true,
		checksum          =  true,
		chown             =  true,
		chmod             =  true,
		compress          =  true,
		copy_dirlinks     =  true,
		copy_links        =  true,
		copy_unsafe_links =  true,
		cvs_exclude       =  true,
		delete_excluded   =  true,
		dry_run           =  true,
		executability     =  true,
		existing          =  true,
		group             =  true,
		groupmap          =  true,
		hard_links        =  true,
		ignore_times      =  true,
		inplace           =  true,
		ipv4              =  true,
		ipv6              =  true,
		keep_dirlinks     =  true,
		links             =  true,
		one_file_system   =  true,
		omit_dir_times    =  true,
		omit_link_times   =  true,
		owner             =  true,
		password_file     =  true,
		perms             =  true,
		protect_args      =  true,
		prune_empty_dirs  =  true,
		quiet             =  true,
		rsh               =  true,
		rsync_path        =  true,
		sparse            =  true,
		suffix            =  true,
		temp_dir          =  true,
		timeout           =  true,
		times             =  true,
		update            =  true,
		usermap           =  true,
		verbose           =  true,
		whole_file        =  true,
		xattrs            =  true,
		_extra            =  true,
	},
}


--
-- Returns true for non Init and Blanket events.
--
local eventNotInitBlank =
	function
(
	event
)
	return event.etype ~= 'Init' and event.etype ~= 'Blanket'
end


--
-- Spawns rsync for a list of events
--
-- Exclusions are already handled by not having
-- events for them.
--
rsync.action = function
(
	inlet
)
	local config = inlet.getConfig( )

	-- gets all events ready for syncing
	local elist = inlet.getEvents( eventNotInitBlank )

	-- gets the list of paths for the event list
	-- deletes create multi match patterns
	local paths = elist.getPaths( )

	-- removes trailing slashes from dirs.
	for k, v in ipairs( paths )
	do
		if string.byte( v, -1 ) == 47
		then
			paths[ k ] = string.sub( v, 1, -2 )
		end
	end

	log(
		'Normal',
		'Calling rsync with filter-list of new/modified files/dirs\n',
		table.concat( paths, '\n' )
	)

	local delete = nil

	if config.delete == true
	or config.delete == 'running'
	then
		delete = { '--delete-missing-args', '--ignore-errors', '--no-implied-dirs' }
	end

	spawn(
		elist,
		config.rsync.binary,
		'<', table.concat( paths, '\000' ),
		config.rsync._computed,
		delete,
		'--force',
		'--from0',
		'--files-from=-',
		config.source,
		config.target
	)
end


--
-- Spawns the recursive startup sync.
--
rsync.init = function
(
	event
)
	local config   = event.config

	local inlet    = event.inlet

	local excludes = inlet.getExcludes( )

	local filters = inlet.hasFilters( ) and inlet.getFilters( )

	local delete   = {}

	local target   = config.target

	if not target
	then
		if not config.host
		then
			error('Internal fail, Neither target nor host is configured')
		end

		target = config.host .. ':' .. config.targetdir
	end

	if config.delete == true
	or config.delete == 'startup'
	then
		delete = { '--delete', '--ignore-errors' }
	end

	if config.rsync.delete_excluded == true
	then
		table.insert( delete, '--delete-excluded' )
	end

	if not filters and #excludes == 0
	then
		-- starts rsync without any filters or excludes
		log(
			'Normal',
			'recursive startup rsync: ',
			config.source,
			' -> ',
			target
		)

		spawn(
			event,
			config.rsync.binary,
			delete,
			config.rsync._computed,
			'-r',
			config.source,
			target
		)

	elseif not filters
	then
		-- starts rsync providing an exclusion list
		-- on stdin
		local exS = table.concat( excludes, '\n' )

		log(
			'Normal',
			'recursive startup rsync: ',
			config.source,
			' -> ',
			target,
			' excluding\n',
			exS
		)

		spawn(
			event,
			config.rsync.binary,
			'<', exS,
			'--exclude-from=-',
			delete,
			config.rsync._computed,
			'-r',
			config.source,
			target
		)
	else
		-- starts rsync providing a filter list
		-- on stdin
		local fS = table.concat( filters, '\n' )

		log(
			'Normal',
			'recursive startup rsync: ',
			config.source,
			' -> ',
			target,
			' filtering\n',
			fS
		)

		spawn(
			event,
			config.rsync.binary,
			'<', fS,
			'--filter=. -',
			delete,
			config.rsync._computed,
			'-r',
			config.source,
			target
		)
	end
end


--
-- Prepares and checks a syncs configuration on startup.
--
rsync.prepare = function
(
	config,    -- the configuration
	level,     -- additional error level for inherited use ( by rsyncssh )
	skipTarget -- used by rsyncssh, do not check for target
)

	-- First let default.prepare test the checkgauge
	default.prepare( config, level + 6 )

	if not skipTarget and not config.target
	then
		error(
			'default.rsync needs "target" configured',
			level
		)
	end

	-- checks if the _computed argument exists already
	if config.rsync._computed
	then
		error(
			'please do not use the internal rsync._computed parameter',
			level
		)
	end

	-- computes the rsync arguments into one list
	local crsync = config.rsync;

	-- everything implied by archive = true
	local archiveFlags = {
		recursive   =  true,
		links       =  true,
		perms       =  true,
		times       =  true,
		group       =  true,
		owner       =  true,
		devices     =  true,
		specials    =  true,
		hard_links  =  false,
		acls        =  false,
		xattrs      =  false,
	}

	-- if archive is given the implications are filled in
	if crsync.archive
	then
		for k, v in pairs( archiveFlags )
		do
			if crsync[ k ] == nil
			then
				crsync[ k ] = v
			end
		end
	end

	crsync._computed = { true }

	local computed = crsync._computed

	local computedN = 2

	local shortFlags = {
		acls               = 'A',
		backup             = 'b',
		checksum           = 'c',
		compress           = 'z',
		copy_dirlinks      = 'k',
		copy_links         = 'L',
		cvs_exclude        = 'C',
		dry_run            = 'n',
		executability      = 'E',
		group              = 'g',
		hard_links         = 'H',
		ignore_times       = 'I',
		ipv4               = '4',
		ipv6               = '6',
		keep_dirlinks      = 'K',
		links              = 'l',
		one_file_system    = 'x',
		omit_dir_times     = 'O',
		omit_link_times    = 'J',
		owner              = 'o',
		perms              = 'p',
		protect_args       = 's',
		prune_empty_dirs   = 'm',
		quiet              = 'q',
		sparse             = 'S',
		times              = 't',
		update             = 'u',
		verbose            = 'v',
		whole_file         = 'W',
		xattrs             = 'X',
	}

	local shorts = { '-' }
	local shortsN = 2

	if crsync._extra
	then
		for k, v in ipairs( crsync._extra )
		do
			computed[ computedN ] = v
			computedN = computedN  + 1
		end
	end

	for k, flag in pairs( shortFlags )
	do
		if crsync[ k ]
		then
			shorts[ shortsN ] = flag
			shortsN = shortsN + 1
		end
	end

	if crsync.devices and crsync.specials
	then
			shorts[ shortsN ] = 'D'
			shortsN = shortsN + 1
	else
		if crsync.devices
		then
			computed[ computedN ] = '--devices'
			computedN = computedN  + 1
		end

		if crsync.specials
		then
			computed[ computedN ] = '--specials'
			computedN = computedN  + 1
		end
	end

	if crsync.append
	then
		computed[ computedN ] = '--append'
		computedN = computedN  + 1
	end

	if crsync.append_verify
	then
		computed[ computedN ] = '--append-verify'
		computedN = computedN  + 1
	end

	if crsync.backup_dir
	then
		computed[ computedN ] = '--backup-dir=' .. crsync.backup_dir
		computedN = computedN  + 1
	end

	if crsync.bwlimit
	then
		computed[ computedN ] = '--bwlimit=' .. crsync.bwlimit
		computedN = computedN  + 1
	end

	if crsync.chmod
	then
		computed[ computedN ] = '--chmod=' .. crsync.chmod
		computedN = computedN  + 1
	end

	if crsync.chown
	then
		computed[ computedN ] = '--chown=' .. crsync.chown
		computedN = computedN  + 1
	end

	if crsync.copy_unsafe_links
	then
		computed[ computedN ] = '--copy-unsafe-links'
		computedN = computedN  + 1
	end

	if crsync.groupmap
	then
		computed[ computedN ] = '--groupmap=' .. crsync.groupmap
		computedN = computedN  + 1
	end

	if crsync.existing
	then
		computed[ computedN ] = '--existing'
		computedN = computedN  + 1
	end

	if crsync.inplace
	then
		computed[ computedN ] = '--inplace'
		computedN = computedN  + 1
	end

	if crsync.password_file
	then
		computed[ computedN ] = '--password-file=' .. crsync.password_file
		computedN = computedN  + 1
	end

	if crsync.rsh
	then
		computed[ computedN ] = '--rsh=' .. crsync.rsh
		computedN = computedN  + 1
	end

	if crsync.rsync_path
	then
		computed[ computedN ] = '--rsync-path=' .. crsync.rsync_path
		computedN = computedN  + 1
	end

	if crsync.suffix
	then
		computed[ computedN ] = '--suffix=' .. crsync.suffix
		computedN = computedN  + 1
	end

	if crsync.temp_dir
	then
		computed[ computedN ] = '--temp-dir=' .. crsync.temp_dir
		computedN = computedN  + 1
	end

	if crsync.timeout
	then
		computed[ computedN ] = '--timeout=' .. crsync.timeout
		computedN = computedN  + 1
	end

	if crsync.usermap
	then
		computed[ computedN ] = '--usermap=' .. crsync.usermap
		computedN = computedN  + 1
	end

	if shortsN ~= 2
	then
		computed[ 1 ] = table.concat( shorts, '' )
	else
		computed[ 1 ] = { }
	end

	-- appends a / to target if not present
	-- and not a ':' for home dir.
	if not skipTarget
	and string.sub( config.target, -1 ) ~= '/'
	and string.sub( config.target, -1 ) ~= ':'
	then
		config.target = config.target..'/'
	end
end


--
-- By default do deletes.
--
rsync.delete = true

--
-- Rsyncd exitcodes
--
rsync.exitcodes  = default.rsyncExitCodes

--
-- Calls rsync with this default options
--
rsync.rsync =
{
	-- The rsync binary to be called.
	binary        = 'rsync',
	links         = true,
	times         = true,
	protect_args  = true
}


--
-- Default delay
--
rsync.delay = 15
