--~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
-- inotify.lua
--
--
-- Interface to inotify core.
--
-- watches recursively subdirs and sends events.
-- All inotify specific implementation is enclosed here.
--
--
-- This code assumes your editor is at least 100 chars wide.
--
-- License: GPLv2 (see COPYING) or any later version
-- Authors: Axel Kittenberger <axkibe@gmail.com>
--
--~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~


if lsyncd_version
then
	print( 'Error, Lsyncd mantle already loaded' )
	os.exit( -1 )
end


--
-- Returns the relative part of absolute path if it
-- begins with root
--
local function splitPath
(
	path,
	root
)
	local rlen = #root

	local sp = string.sub( path, 1, rlen )

	if sp == root
	then
		return string.sub( path, rlen, -1 )
	else
		return nil
	end
end

--
-- A list indexed by inotify watch descriptors yielding
-- the directories absolute paths.
--
local wdpaths = Counter.new( )

--
-- The same vice versa,
-- all watch descriptors by their absolute paths.
--
local pathwds = { }

--
-- A list indexed by syncs containing yielding
-- the root paths the syncs are interested in.
--
local syncRoots = { }

--
-- Stops watching a directory
--
local function removeWatch
(
	path,  -- absolute path to unwatch
	core   -- if false not actually send the unwatch to the kernel
	--        ( used in moves which reuse the watch )
)
	local wd = pathwds[ path ]

	if not wd then return end

	if core then core.inotify.rmwatch( wd ) end

	wdpaths[ wd ] = nil
	pathwds[ path ] = nil
end

--
-- Adds watches for a directory (optionally) including all subdirectories.
--
local function addWatch
(
	path  -- absolute path of directory to observe
)
	log( 'Function', 'Inotify.addWatch( ', path, ' )' )

	if not Syncs.concerns( path )
	then
		log( 'Inotify', 'not concerning "', path, '"')

		return
	end

	-- registers the watch
	local inotifyMode = ( uSettings and uSettings.inotifyMode ) or ''

	local wd = core.inotify.addwatch( path, inotifyMode )

	if wd < 0
	then
		log( 'Inotify', 'Unable to add watch "', path, '"' )

		return
	end

	do
		-- If this watch descriptor is registered already
		-- the kernel reuses it since the old dir is gone.
		local op = wdpaths[ wd ]

		if op and op ~= path then pathwds[ op ] = nil end
	end

	pathwds[ path ] = wd

	wdpaths[ wd ] = path

	-- registers and adds watches for all subdirectories
	local entries = core.readdir( path )

	if not entries then return end

	for dirname, isdir in pairs( entries )
	do
		if isdir then addWatch( path .. dirname .. '/' ) end
	end
end

--
-- Adds a Sync to receive events.
--
local function addSync
(
	sync,     -- object to receive events.
	rootdir   -- root dir to watch
)
	if syncRoots[ sync ]
	then
		error( 'duplicate sync in Inotify.addSync()' )
	end

	syncRoots[ sync ] = rootdir

	addWatch( rootdir )
end

--
-- Called when an event has occured.
--
local function event
(
	etype,     -- 'Attrib', 'Modify', 'Create', 'Delete', 'Move'
	wd,        --  watch descriptor, matches core.inotifyadd()
	isdir,     --  true if filename is a directory
	time,      --  time of event
	filename,  --  string filename without path
	wd2,       --  watch descriptor for target if it's a Move
	filename2  --  string filename without path of Move target
)
	if isdir
	then
		filename = filename .. '/'

		if filename2 then filename2 = filename2 .. '/' end
	end

	if filename2
	then
		log(
			'Inotify',
			'got event ', etype, ' ',
			filename, '(', wd, ') to ',
			filename2, '(', wd2 ,')'
		)
	else
		log(
			'Inotify',
			'got event ', etype, ' ',
			filename, '(', wd, ')'
		)
	end

	-- looks up the watch descriptor id
	local path = wdpaths[ wd ]

	if path then path = path..filename end

	local path2 = wd2 and wdpaths[ wd2 ]

	if path2 and filename2 then path2 = path2..filename2 end

	if not path and path2 and etype == 'Move'
	then
		log( 'Inotify', 'Move from deleted directory ', path2, ' becomes Create.' )

		path  = path2

		path2 = nil

		etype = 'Create'
	end

	if not path
	then
		-- this is normal in case of deleted subdirs
		log( 'Inotify', 'event belongs to unknown watch descriptor.' )

		return
	end

	for sync, root in pairs( syncRoots )
	do repeat
		local relative  = splitPath( path, root )

		local relative2 = nil

		if path2 then relative2 = splitPath( path2, root ) end

		if not relative and not relative2
		then
			-- sync is not interested in this dir
			break -- continue
		end

		-- makes a copy of etype to possibly change it
		local etyped = etype

		if etyped == 'Move'
		then
			if not relative2
			then
				log(
					'Normal',
					'Transformed Move to Delete for ',
					sync.config.name
				)

				etyped = 'Delete'
			elseif not relative
			then
				relative = relative2

				relative2 = nil

				log(
					'Normal',
					'Transformed Move to Create for ',
					sync.config.name
				)

				etyped = 'Create'
			end
		end

		if isdir
		then
			if etyped == 'Create'
			then
				addWatch( path )
			elseif etyped == 'Delete'
			then
				removeWatch( path, true )
			elseif etyped == 'Move'
			then
				removeWatch( path, false )
				addWatch( path2 )
			end
		end

		sync:delay( etyped, time, relative, relative2 )

	until true end
end

--
-- Writes a status report about inotify to a file descriptor
--
local function statusReport( f )

	f:write( 'Inotify watching ', #wdpaths, ' directories\n' )

	for wd, path in pairs( wdpaths )
	do
		f:write( '  ', wd, ': ', path, '\n' )
	end
end

--
-- Exported interface.
--
Inotify =
{
	addSync = addSync,
	event = event,
	statusReport = statusReport,
}
