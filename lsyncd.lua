------------------------------------------------------------------------------
-- lsyncd.lua   Live (Mirror) Syncing Demon
--
-- License: GPLv2 (see COPYING) or any later version
--
-- Authors: Axel Kittenberger <axkibe@gmail.com>
--
-- This is the "runner" part of lsyncd. It containts all its high-level logic.
-- It works closely together with the lsyncd core in lsyncd.c. This means it
-- cannot be runned directly from the standard lua interpreter.
------------------------------------------------------------------------------

----
-- A security measurement.
-- Core will exit if version ids mismatch.
if lsyncd_version ~= nil then
	-- checks if the runner is being loaded twice 
	print("You cannot use the lsyncd runner as configuration file!")
	os.exit(-1)
end
lsyncd_version = "2.0b1"

----
-- Shortcuts (which user is supposed to be able to use them as well)
--
log  = lsyncd.log
exec = lsyncd.exec

----
-- Table of all root directories to sync, 
-- filled during initialization.
--
-- [#] {
--    actions     = actions, 
--    source      = source_dir, 
--    targetident = target_identifier,
--    target      = link to target directory
-- }
--
origins = {}

----
-- a array of all targets
--
-- structure:
-- [#] {
--    .ident           .. the identifier of target (like string "host:dir")
--                        for lsyncd this passed competly opaquely to the 
--                        action handlers
--    .delays = [#) {  .. the delays stack
--         .atype      .. enum, kind of action
--         .wd         .. watch descriptor id this origins from TODO needed?
--         .attend     .. link to atttender that raised this delay.
--         .filename   .. filename or nil (=dir itself)
--         (.movepeer) .. for MOVEFROM/MOVETO link to other delay
--    }
-- }
--
targets = {}

-----
-- all watches
--
-- structure: 
-- [wd] = {
--     .wd      ..              the watch descriptor (TODO needed?)
--     .syncs = [#] {			list of stuff to sync to
--         .origin .. link to origin
--         .path   .. relative path of dir
--         .parent .. link to parent directory in watches
--                    or nil for origin
--     }
-- }
--
watches = {}

------
-- TODO
collapse_table = {
	[ATTRIB] = { [ATTRIB] = ATTRIB, [MODIFY] = MODIFY, [CREATE] = CREATE, [DELETE] = DELETE },
	[MODIFY] = { [ATTRIB] = MODIFY, [MODIFY] = MODIFY, [CREATE] = CREATE, [DELETE] = DELETE },
	[CREATE] = { [ATTRIB] = CREATE, [MODIFY] = CREATE, [CREATE] = CREATE, [DELETE] = DELETE },
	[DELETE] = { [ATTRIB] = DELETE, [MODIFY] = DELETE, [CREATE] = MODIFY, [DELETE] = DELETE },
}

-----
-- A list of names of the event types the core sends.
--
local event_names = {
	[ATTRIB   ] = "Attrib",
	[MODIFY   ] = "Modify",
	[CREATE   ] = "Create",
	[DELETE   ] = "Delete",
	[MOVE     ] = "Move",
	[MOVEFROM ] = "MoveFrom",
	[MOVETO   ] = "MoveTo",
}

-----
-- TODO
local function delay_action(atype, wd, sync, filename, time)
	print("delay_action "..event_names[atype].."("..wd..") ")
	-- TODO
end

----
-- Adds watches for a directory including all subdirectories.
--
-- @param origin    link to origins[] entry
-- @param path      relative path of this dir to origin
-- @param parent    link to parent directory in watches[]
--
local function attend_dir(origin, path, parent)
	local op = origin.source .. path
	-- register watch and receive watch descriptor
	local wd = lsyncd.add_watch(op);
	if wd < 0 then
		-- failed adding the watch
		log(ERROR, "Failure adding watch "..op.." -> ignored ")
		return
	end

	local thiswatch = watches[wd]
	if thiswatch == nil then
		-- new watch
		thiswatch = {wd = wd, syncs = {} }
		watches[wd] = thiswatch
	end
	local sync = { origin = origin, path = path, parent = parent } 
	table.insert(thiswatch.syncs, sync)

	-- warmstart?
	if origin.actions.startup == nil then
		delay_action(CREATE, wd, sync, nil, nil)
	end

	-- register all subdirectories 
	local subdirs = lsyncd.sub_dirs(op);
	for i, dirname in ipairs(subdirs) do
		attend_dir(origin, path..dirname.."/", thiswatch)
	end
end

----
-- Called from core on init or restart after user configuration.
-- 
function lsyncd_initialize()
	-- makes sure the user gave lsyncd anything to do 
	if #origins == 0 then
		log(ERROR, "nothing to watch. Use directory(SOURCEDIR, TARGET) in your config file.");
		lsyncd.terminate(-1) -- ERRNO
	end

	-- set to true if at least one origin has a startup function
	local have_startup = false
	-- runs through the origins table filled by user calling directory()
	for i, origin in ipairs(origins) do
		-- resolves source to be an absolute path
		local asrc = lsyncd.real_dir(origin.source)
		if asrc == nil then
			print("Cannot resolve source path: ", origin.source)
			lsyncd.terminate(-1) -- ERRNO
		end
		origin.source = asrc

		-- appends the target on target lists
		local target = { ident = origin.targetident, delays = {} }
		table.insert(targets, target)
		origin.target = target 
		if origin.actions.startup ~= nil then
			have_startup = true
		end
		-- and add the dir watch inclusively all subdirs
		attend_dir(origin, "", nil)
	end
	
	if have_startup then
		log(NORMAL, "--- startup ---")
		local pids = { }
		for i, origin in ipairs(origins) do
			local pid
			if origin.actions.startup ~= nil then
				local pid = origin.actions.startup(origin.source, origin.targetident)
				table.insert(pids, pid)
			end
		end
		lsyncd.wait_pids(pids, "startup_collector")
		log(NORMAL, "--- Entering normal operation with "..#watches.." monitored directories ---")
	else
		log(NORMAL, "--- Warmstart into normal operation with "..#watches.." monitored directories ---")
	end
end

----
-- Called by core to query soonest alarm.
--
-- @return two variables.
--         number -1 means ... alarm is in the past.
--                 0 means ... no alarm, core can in untimed sleep
--                 1 means ... alarm time specified.
--         times           ... the alarm time (only read if number is 1)
function lsyncd_get_alarm()
	return 0, 0
end

-----
-- Called by core on inotify event
--
-- @param etype     enum (ATTRIB, MODIFY, CREATE, DELETE, MOVE)
-- @param wd        watch descriptor (matches lsyncd.add_watch())
-- @param filename  string filename without path
-- @param filename2 
--
function lsyncd_event(etype, wd, isdir, filename, filename2)
	local ftype;
	if isdir then
		ftype = "directory"
	else
		ftype = "file"
	end
	-- TODO comment out to safe performance
	if filename2 == nil then
		log(DEBUG, "got event "..event_names[etype].." of "..ftype.." "..filename) 
	else 
		log(DEBUG, "got event "..event_names[etype].." of "..ftype.." "..filename.." to "..filename2) 
	end

	-- looks up the watch descriptor id
	local w = watches[wd]
	if w == nil then
		log(NORMAL, "event belongs to unknown or deleted watch descriptor.")
		return
	end
	
	-- works through all possible source->target pairs
	for i, sync in ipairs(w.syncs) do
		time = nil -- TODO
		delay_action(etype, wd, sync, filename, time)

		-- add subdirs for new directories
		if isdir and eypte == CREATE then
			attend_dir(sync.origin, sync.path..filename.."/", w)
		end
	end
end

-----
-- Called by the core for every child process that 
-- finished in startup phase
--
-- Parameters are pid and exitcode of child process
--
-- Can return either a new pid if one other child process 
-- has been spawned as replacement (e.g. retry) or 0 if
-- finished/ok.
--
function startup_collector(pid, exitcode)
	if exitcode ~= 0 then
		log(ERROR, "Startup process", pid, " failed")
		lsyncd.terminate(-1) -- ERRNO
	end
	return 0
end


------------------------------------------------------------------------------
-- lsyncd user interface
------------------------------------------------------------------------------

----
-- Adds one directory (incl. subdir) to be synchronized.
-- Users primary configuration device.
--
-- @param TODO
--
function sync(source_dir, target_identifier, actions)
	local o = {     actions = actions, 
	                source  = source_dir, 
	            targetident = target_identifier, 
	}
	table.insert(origins, o)
	return 
end

----
-- Called by core when an overflow happened.
function default_overflow()
	log(ERROR, "--- OVERFLOW on inotify event queue ---")
	lsyncd.terminate(-1) -- TODO reset instead.

end
overflow = default_overflow

