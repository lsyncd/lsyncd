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
log  = lsyncd.log
exec = lsyncd.exec

----
-- Table of all directories to watch, 
-- filled and used only during initialization.
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
--         .attend     .. link to atttender that raised this.
--         .filename   .. filename or nil, means dir itself
--         (.movepeer) .. for MOVEFROM/MOVETO link to other delay
--    }
-- }
targets = {}

-----
-- all watches
--
-- structure: 
-- [wd] = {
--     .wd      ..               the watch descriptor (TODO needed?)
--     .targets = [#] {
--                    .odir   .. origin source dir
--                    .path   .. path of dir
--                    .target .. link to targets[#]
--                    .parent .. link to parent directory in watches
--                               or nil for origin
--                }
-- }
--
watches = {}

-- TODO
collapse_table = {
	[ATTRIB] = { [ATTRIB] = ATTRIB, [MODIFY] = MODIFY, [CREATE] = CREATE, [DELETE] = DELETE },
	[MODIFY] = { [ATTRIB] = MODIFY, [MODIFY] = MODIFY, [CREATE] = CREATE, [DELETE] = DELETE },
	[CREATE] = { [ATTRIB] = CREATE, [MODIFY] = CREATE, [CREATE] = CREATE, [DELETE] = DELETE },
	[DELETE] = { [ATTRIB] = DELETE, [MODIFY] = DELETE, [CREATE] = MODIFY, [DELETE] = DELETE },
}


-----
-- TODO
local function delay_action(atype, target, time, wd, odir, path)
	-- TODO
end

----
-- Adds watches for a directory including all subdirectories.
--
-- @param odir      origin dir
-- @param path      path in this dir
-- @param target    link to target in [targets]
-- @param parent    link to parent directory in watches[]
-- @param actions   TODO
--
local function attend_dir(odir, path, target, parent, actions)
	-- actual dir = origin + path 
	local op = odir .. path
	-- register watch and receive watch descriptor
	local wd = lsyncd.add_watch(op);
	if wd < 0 then
		-- failed adding the watch
		log(ERROR, "Failure adding watch " .. op .." -> ignored ")
		-- TODO die?
		return
	end

	local thiswatch = watches[wd]
	if thiswatch == nil then
		-- new watch
		thiswatch = {wd = wd, attends = {} }
		watches[wd] = thiswatch
	end
	table.insert(thiswatch.attends, { odir = odir, path = path, target = target, parent = parent, actions = actions })

	-- register all subdirectories 
	local subdirs = lsyncd.sub_dirs(op);
	for i, dirname in ipairs(subdirs) do
		attend_dir(odir, path .. dirname .. "/", target, thiswatch, actions)
	end

	-- TODO
	if actions ~= nil then
		delay_action(CREATE, target, nil, nil, wd, odir, path)
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

	-- runs through the origins table filled by user calling directory()
	for i, o in ipairs(origins) do
		-- resolves source to be an absolute path
		local asrc = lsyncd.real_dir(o.source)
		if asrc == nil then
			print("Cannot resolve source path: ", o.source)
			lsyncd.terminate(-1) -- ERRNO
		end

		-- appends the target on target lists
		local target = { ident = o.targetident, delays = {} }
		table.insert(targets, target)
		o.target = target  -- TODO needed?

		-- and add the dir watch inclusively all subdirs
		attend_dir(asrc, "", target, nil)
	end
	
	log(NORMAL, "--- startup ---")
	local pids = { }
	local pid
	for i, o in ipairs(origins) do
		if (o.actions.startup ~= nil) then
			pid = o.actions.startup(o.source, o.targetident)
		end
		table.insert(pids, pid)
	end
	lsyncd.wait_pids(pids, "startup_collector")
	log(NORMAL, "--- Entering normal operation with " .. #watches .. " monitored directories ---")
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
		log(DEBUG, "got event " .. event_names[etype] .. " of " .. ftype .. " " .. filename) 
	else 
		log(DEBUG, "got event " .. event_names[etype] .. " of " .. ftype .. " " .. filename .. " to " .. filename2) 
	end

	-- looks up the watch descriptor id
	local w = watches[wd]
	if w == nil then
		log(NORMAL, "event belongs to unknown or deleted watch descriptor.")
		return
	end
	
	-- works through all possible source->target pairs
	for i, a in ipairs(w.attends) do
		log(DEBUG, "odir = " .. a.odir .. " path = " .. a.path)
		if (isdir) then
			if (etype == CREATE) then
				attend_dir(a.odir, a.path .. filename .. "/", w, a.actions)
			end
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
local function startup_collector(pid, exitcode)
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
function sync(actions, source_dir, target_identifier)
	local o = { actions = actions, 
	            source = source_dir, 
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



