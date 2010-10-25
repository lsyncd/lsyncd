--============================================================================
-- lsyncd.lua   Live (Mirror) Syncing Demon
--
-- License: GPLv2 (see COPYING) or any later version
--
-- Authors: Axel Kittenberger <axkibe@gmail.com>
--
-- This is the "runner" part of lsyncd. It containts all its high-level logic.
-- It works closely together with the lsyncd core in lsyncd.c. This means it
-- cannot be runned directly from the standard lua interpreter.
--============================================================================

----
-- A security measurement.
-- Core will exit if version ids mismatch.
--
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

--============================================================================
-- Coding checks, ensure termination on some easy to do coding errors.
--============================================================================

-----
-- Metatable to limit keys to numerics.
--
local meta_check_array = {
	__index = function(t, k) 
		if type(k) ~= "number" then
			error("This table is an array and must have numeric keys", 2)
		end
		return rawget(t, k)
	end,
	__newindex = function(t, k, v)
		if type(k) ~= "number" then
			error("This table is an array and must have numeric keys", 2)
		end
		rawset(t, k, v)
	end
}

-----
-- Metatable to limit keys to numerics and count the number of entries.
-- Lua's # operator does not work on tables which key values are not 
-- strictly linear.
--
local meta_check_count_array = {
	__index = function(t, k) 
		if k == size then
			return rawget(t, "size")
		end
		if type(k) ~= "number" then
			error("This table is an array and must have numeric keys", 2)
		end
		return rawget(t, "_t")[k]
	end,

	__newindex = function(t, k, v)
		if type(k) ~= "number" then
			error("This table is an array and must have numeric keys", 2)
		end
		local _t = rawget(t, "_t")
		local vb = _t[k]
		if v and not vb then
			rawset(t, "size", rawget(t, "size") + 1)
		elseif not v and vb then
			rawset(t, "size", rawget(t, "size") - 1)
		end
		_t[k] = v
	end
}


-----
-- Metatable to limit keys to those only presented in their prototype
--
local meta_check_prototype = {
	__index = function(t, k) 
		if not t.prototype[k] then
			error("This table does not have key '"..k.."' in its prototype.", 2)
		end
		return rawget(t, k)
	end,
	__newindex = function(t, k, v)
		if not t.prototype[k] then
			error("This table does not have key '"..k.."' in its prototype.", 2)
		end
		rawset(t, k, v)
	end
}

-----
-- Limits the keys of table to numbers.
--
local function set_array(t)
	for k, _ in pairs(t) do
		if type(k) ~= "number" then
			error("This table cannot be set as array, it has non-numberic key '"..k.."'", 2)
		end
	end
	setmetatable(t, meta_check_array)
end

-----
-- Creates a table with keys limited to numbers.
--
local function new_array()
	local t = {}
	setmetatable(t, meta_ckeck_array)
	return t
end

-----
-- Creates a table with keys limited to numbers and
-- which counts the number of entries
--
local function new_count_array()
	local t = { size = 0, _t = {} }
	setmetatable(t, meta_check_count_array)
	return t
end


-----
-- Sets the prototype of a table limiting its keys to a defined list.
--
local function set_prototype(t, prototype) 
	t.prototype = prototype
	for k, _ in pairs(t) do
		if not t.prototype[k] and k ~= "prototype" then
			error("Cannot set prototype of table, conflicting key: '"..k.."'.", 2)
		end
	end
	setmetatable(t, proto_check_table)
end

----
-- Locks a table
local function GLOBAL_lock(t)
	local mt = getmetatable(t) or {}
	mt.__newindex = lock_new_index
	setmetatable(t, mt)
end

----
-- Unlocks a table
---
local function GLOBAL_unlock(t)
	local mt = getmetatable(t) or {}
	mt.__newindex = unlock_new_index
	setmetatable(t, mt)
end

-----
-- ?
local function lock_new_index(t, k, v)
	if (k~="_" and string.sub(k,1,2) ~= "__") then
		GLOBAL_unlock(_G)
		error("Lsyncd does not allow GLOBALS to be created on the fly." ..
		      "Declare '" ..k.."' local or declare global on load.", 2)
	else
		rawset(t, k, v)
	end
end

-----
-- ?
local function unlock_new_index(t, k, v)
	rawset(t, k, v)
end


--============================================================================
-- Lsyncd globals
--============================================================================


----
-- Table of all root directories to sync, 
-- filled during initialization.
--
-- [#] {
--    actions     = actions, 
--    source      = source_dir, 
--    targetident = the identifier of target (like string "host:dir")
--                  for lsyncd this passed competly opaquely to the 
--                  action handlers
--
--    .processes = [pid] .. a sublist of processes[] for this target
--    .delays = [#) {    .. the delays stack
--         .atype        .. enum, kind of action
--         .alarm        .. when it should fire
--         .wd           .. watch descriptor id this origins from TODO needed?
--         .sync         .. link to sync that raised this delay.
--         .filename     .. filename or nil (=dir itself)
--         (.movepeer)   .. for MOVEFROM/MOVETO link to other delay
--    }
-- }
--
local origins = new_array()
local proto_delay = {atype=true, alarm=true, wd=true, sync=true, filename=true, movepeer=true}

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
local watches = new_count_array()
local proto_watch = {wd=true, syncs=true}
local proto_sync  = {origin=true, path=true, parent=true}


-----
-- a dictionary of all processes lsyncd spawned.
--
-- structure
-- [pid] = {
--     target   ..
--     atype    .. 
--     wd       ..
--     sync     .. 
--     filename ..
--
local processes = new_count_array()
local proto_process = {pid=true, atype=true, wd=true, sync=true, filename=true}

------
-- TODO
--
local collapse_table = {
	[ATTRIB] = { [ATTRIB] = ATTRIB, [MODIFY] = MODIFY, [CREATE] = CREATE, [DELETE] = DELETE },
	[MODIFY] = { [ATTRIB] = MODIFY, [MODIFY] = MODIFY, [CREATE] = CREATE, [DELETE] = DELETE },
	[CREATE] = { [ATTRIB] = CREATE, [MODIFY] = CREATE, [CREATE] = CREATE, [DELETE] = DELETE },
	[DELETE] = { [ATTRIB] = DELETE, [MODIFY] = DELETE, [CREATE] = MODIFY, [DELETE] = DELETE },
}
set_array(collapse_table)

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
set_array(event_names)

--============================================================================
-- The lsyncd runner 
--============================================================================

-----
-- Puts an action on the delay stack.
--
local function delay_action(atype, wd, sync, filename, time)
	log(DEBUG, "delay_action "..event_names[atype].."("..wd..") ")
	local origin = sync.origin
	local delays = origin.delays
	local nd = {atype    = atype, 
	            wd       = wd, 
	            sync     = sync, 
	            filename = filename }
	set_prototype(nd, proto_delay)
	if time ~= nil and origin.actions.delay ~= nil then
		nd.alarm = lsyncd.addto_clock(time, origin.actions.delay)
	else
		nd.alarm = lsyncd.now()
	end
	table.insert(delays, nd)
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
		set_prototype(thiswatch, proto_watch)
		watches[wd] = thiswatch
	end
	local sync = { origin = origin, path = path, parent = parent } 
	set_prototype(sync, proto_sync)
	table.insert(thiswatch.syncs, sync)

	-- warmstart?
	if origin.actions.startup == nil then
		delay_action(CREATE, wd, sync, nil, nil)
	end

	-- registers and adds watches for all subdirectories 
	local subdirs = lsyncd.sub_dirs(op);
	for _, dirname in ipairs(subdirs) do
		attend_dir(origin, path..dirname.."/", thiswatch)
	end
end

-----
-- Called from code whenever a child process finished and 
-- zombie process was collected by core.
--
function lsyncd_collect_process(pid, exitcode) 
	log(DEBUG, "collected "..pid)
	local process = processes[pid]
	if process == nil then
		return
	end
	local sync = process.sync
	local o = sync.origin
	print("collected ", pid, ": ", event_names[process.atype], o.source, "/", sync.path , process.filename, " = ", exitcode)
	processes[pid] = nil
	o.processes[pid] = nil
end

-----
-- TODO
--
--
local function invoke_action(delay)
	local sync    = delay.sync
	local origin  = sync.origin
	local actions = origin.actions
	local func = nil
	if delay.atype == CREATE then
		if actions.create ~= nil then
			func = actions.create
		elseif actions.default ~= nil then
			func = actions.default
		end
	elseif delay.atype == ATTRIB then
		if actions.attrib ~= nil then
			func = actions.attrib
		elseif actions.default ~= nil then
			func = actions.default
		end
	elseif delay.atype == MODIFY then
		if actions.modify ~= nil then
			func = actions.modify
		elseif actions.default ~= nil then
			func = actions.default
		end
	elseif delay.atype == DELETE then
		if actions.delete ~= nil then
			func = actions.delete
		elseif actions.default ~= nil then
			func = actions.default
		end
	elseif delay.atype == MOVE then
		log(ERROR, "MOVE NOT YET IMPLEMENTED!")
	end
	
	if func ~= nil then
		local pid = func(origin.source, sync.path, delay.filename, origin.targetident)
		if pid ~= nil and pid > 0 then
			local process = {pid      = pid,
			                 atype    = delay.atype,
			                 wd       = delay.wd,
			                 sync     = delay.sync,
			                 filename = delay.filename
			}
			set_prototype(process, proto_process)
			processes[pid] = process
			origin.processes[pid] = process
		end
	end
end
	

----
-- Called from core everytime at the latest of an 
-- expired alarm (or more often)
--
-- @param now   the time now
--
function lsyncd_alarm(now)
	-- goes through all targets and spawns more actions
	-- if possible
	for _, o in ipairs(origins) do
		if o.processes.size < o.actions.max_processes then
			local delays = o.delays
			if delays[1] ~= nil and lsyncd.before_eq(delays[1].alarm, now) then
				invoke_action(o.delays[1])
				table.remove(delays, 1)
			end
		end
	end
end

----
-- Called from core on init or restart after user configuration.
-- 
function lsyncd_initialize(args)
	-- From this point on, no globals may be created anymore
	GLOBAL_lock(_G)

	print(table.concat(args))

	-- makes sure the user gave lsyncd anything to do 
	if #origins == 0 then
		log(ERROR, "nothing to watch. Use directory(SOURCEDIR, TARGET) in your config file.");
		lsyncd.terminate(-1) -- ERRNO
	end

	-- set to true if at least one origin has a startup function
	local have_startup = false
	-- runs through the origins table filled by user calling directory()
	for _, origin in ipairs(origins) do
		-- resolves source to be an absolute path
		local asrc = lsyncd.real_dir(origin.source)
		local actions = origin.actions
		if asrc == nil then
			print("Cannot resolve source path: ", origin.source)
			lsyncd.terminate(-1) -- ERRNO
		end
		origin.source = asrc
		origin.delays = new_count_array()
		origin.processes = new_count_array()

		if actions.max_processes == nil then
			actions.max_processes = 1 -- TODO DEFAULT MAXPROCESS
		end
		if actions.startup ~= nil then
			have_startup = true
		end

		-- and add the dir watch inclusively all subdirs
		attend_dir(origin, "", nil)
	end
	
	if have_startup then
		log(NORMAL, "--- startup ---")
		local pids = { }
		for _, origin in ipairs(origins) do
			local pid
			if origin.actions.startup ~= nil then
				local pid = origin.actions.startup(origin.source, origin.targetident)
				table.insert(pids, pid)
			end
		end
		lsyncd.wait_pids(pids, "startup_collector")
		log(NORMAL, "--- Entering normal operation with "..watches.size.." monitored directories ---")
	else
		log(NORMAL, "--- Warmstart into normal operation with "..watches.size.." monitored directories ---")
	end
end

----
-- Called by core to query soonest alarm.
--
-- @return two variables.
--         boolean false   ... no alarm, core can in untimed sleep
--                 true    ... alarm time specified.
--         times           ... the alarm time (only read if number is 1)
function lsyncd_get_alarm()
	local have_alarm = false
	local alarm = 0
	for _, o in ipairs(origins) do
		if o.delays[1] ~= nil and 
		   o.processes.size < o.actions.max_processes then
			if have_alarm then
				alarm = lsyncd.earlier(alarm, o.delays[1].alarm)
			else
				alarm = o.delays[1].alarm
				have_alarm = true
			end
		end
	end
	return have_alarm, alarm
end

-----
-- Called by core on inotify event
--
-- @param etype     enum (ATTRIB, MODIFY, CREATE, DELETE, MOVE)
-- @param wd        watch descriptor (matches lsyncd.add_watch())
-- @param time      time of event
-- @param filename  string filename without path
-- @param filename2 
--
function lsyncd_event(etype, wd, isdir, time, filename, filename2)
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
	for _, sync in ipairs(w.syncs) do
		delay_action(etype, wd, sync, filename, time)
		-- add subdirs for new directories
		if isdir then
			if etype == CREATE then
				attend_dir(sync.origin, sync.path..filename.."/", w)
			end
		end
	end
end

-----
-- Collector for every child process that finished in startup phase
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


--============================================================================
-- lsyncd user interface
--============================================================================

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
	if actions.max_actions == nil then
		actions.max_actions = 1
	end
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


