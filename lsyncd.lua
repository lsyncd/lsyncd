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
if lsyncd_version then
	-- checks if the runner is being loaded twice 
	io.stderr:write(
		"You cannot use the lsyncd runner as configuration file!\n")
	os.exit(-1)
end
lsyncd_version = "2.0beta1"

----
-- Shortcuts (which user is supposed to be able to use them as well)
--
log  = lsyncd.log
exec = lsyncd.exec
terminate = lsyncd.terminate

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
		if type(k) ~= "number" then
			error("This table is an array and must have numeric keys", 2)
		end
		return t.nt[k]
	end,

	__newindex = function(t, k, v)
		if type(k) ~= "number" then
			error("This table is an array and must have numeric keys", 2)
		end
		local vb = t.nt[k]
		if v and not vb then
			t.size = t.size + 1
		elseif not v and vb then
			t.size = t.size - 1
		end
		t.nt[k] = v
	end
}


-----
-- Metatable to limit keys to those only presented in their prototype
--
local meta_check_prototype = {
	__index = function(t, k) 
		if not t.prototype[k] then
			error("tables prototype doesn't have key '"..k.."'.", 2)
		end
		return rawget(t, k)
	end,
	__newindex = function(t, k, v)
		if not t.prototype[k] then
			error("tables prototype doesn't have key '"..k.."'.", 2)
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
			error("table can't become an array, since it has key '"..k.."'", 2)
		end
	end
	setmetatable(t, meta_check_array)
end

-----
-- Creates a table with keys limited to numbers.
--
local function new_array()
	local t = {}
	setmetatable(t, meta_check_array)
	return t
end

-----
-- Creates a table with keys limited to numbers and
-- which counts the number of entries
--
local function new_count_array()
	local t = { size = 0, nt = {} }
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
			error("Cannot set prototype, conflicting key: '"..k.."'.", 2)
		end
	end
	setmetatable(t, meta_check_prototype)
end

----
-- Locks globals,
-- no more globals can be created
--
local function globals_lock()
	local t = _G
	local mt = getmetatable(t) or {}
	mt.__index = function(t, k) 
		if (k~="_" and string.sub(k, 1, 2) ~= "__") then
			error("Access of non-existing global.", 2)
		else
			rawget(t, k)
		end
	end
	mt.__newindex = function(t, k, v) 
		if (k~="_" and string.sub(k, 1, 2) ~= "__") then
			error("Lsyncd does not allow GLOBALS to be created on the fly." ..
			      "Declare '" ..k.."' local or declare global on load.", 2)
		else
			rawset(t, k, v)
		end
	end
	setmetatable(t, mt)
end


--============================================================================
-- Lsyncd globals
--============================================================================


----
-- origins 
--
-- table of all root directories to sync.
-- filled during initialization.
--
-- [#] {
--    config      = config, 
--    source      = source_dir, 
--    targetident = the identifier of target (like string "host:dir")
--                  for lsyncd this passed competly opaquely to the 
--                  action handlers
--
--    .processes = [pid] .. a sublist of processes[] for this target
--    .delays = [#) {    .. the delays stack
--         .ename        .. enum, kind of action
--         .alarm        .. when it should fire
--         .pathname     .. complete path relativ to watch origin
--         (.movepeer)   .. for MOVEFROM/MOVETO link to other delay
--    }
--    .delayname[pathname] = [#]  .. a list of lists of all delays from a 
--                                   its pathname.
-- }
--
local origins = new_array()
local proto_origin = {
		config=true, source=true, targetident=true, 
		processes=true, delays=true, delayname=true
	}
local proto_delay  = {
		ename   =true, alarm=true, pathname=true, movepeer=true
	}

-----
-- inotifies
--
-- contains all inotify watches.
--
-- structure:
--    a list indexed by watch descriptor
-- [wd] 
--    of a numeric list of all origins watching this dir.
-- [#]
--    of inotify {
--         .origin .. link to origin
--         .path   .. relative path of dir
--     }
-- }
--
local inotifies = new_count_array()
local proto_inotify = {origin=true, path=true}

-----
-- A list of names of the event types the core sends.
-- (Also makes sure the strings are not collected)
--
local valid_events = {
	Attrib = true,
	Modify = true,
	Create = true,
	Delete = true,
	Move = true,
	MoveFrom = true,
	MoveTo = true,
}

--============================================================================
-- The lsyncd runner 
--============================================================================

-----
-- Puts an action on the delay stack.
--
local function delay_action(ename, wd, time, origin, pathname, pathname2)
	log(DEBUG, "delay_action "..ename.."("..wd..") ")
	local o  = origin
	local delays = o.delays
	local delayname = o.delayname

	if ename == "Move" and not o.config.move then
		-- if there is no move action defined, split a move as delete/create
		log(DEBUG, "splitting Move into Delete & Create")
		delay_action("Delete", wd, time, pathname,  nil)
		delay_action("Create", wd, time, pathname2, nil)
		return
	end

	-- creates the new action
	local newd = {ename    = ename, 
	              pathname = pathname }
	set_prototype(newd, proto_delay)
	if time and o.config.delay then
		newd.alarm = lsyncd.addto_clock(time, o.config.delay)
	else
		newd.alarm = lsyncd.now()
	end

	local oldd = delayname[pathname] 
	if oldd then
		-- if there is already a delay on this pathname.
		-- decide what should happen with multiple delays.
		if newd.ename == "MoveFrom" or newd.ename == "MoveTo" or
		   oldd.ename == "MoveFrom" or oldd.ename == "MoveTo" then
		   -- do not collapse moves
			log(NORMAL, "Not collapsing events with moves on "..pathname)
			-- TODO stackinfo
			return
		else
			local col = o.config.collapse_table[oldd.ename][newd.ename]
			if col == -1 then
				-- events cancel each other
				log(NORMAL, "Nullfication: " ..newd.ename.." after "..
					oldd.ename.." on "..pathname)
				oldd.ename = "none"
				return
			elseif col == 0 then
				-- events tack
				log(NORMAL, "Stacking " ..newd.ename.." after "..
					oldd.ename.." on "..pathname)
				-- TODO Stack pointer
			else
				log(NORMAL, "Collapsing "..newd.ename.." upon "..
					oldd.ename.." to " ..
					col.." on "..pathname)
				oldd.ename = col
				return
			end
		end
		table.insert(delays, newd)
	else
		delayname[pathname] = newd
		table.insert(delays, newd)
	end
end

----
-- Adds watches for a directory including all subdirectories.
--
-- @param origin    link to origins[] entry
-- @param path      relative path of this dir to origin
-- @param parent    link to parent directory in watches[]
--
local function inotify_watch_dir(origin, path)
	local op = origin.source .. path
	-- register watch and receive watch descriptor
	local wd = lsyncd.add_watch(op);
	if wd < 0 then
		-- failed adding the watch
		log(ERROR, "Failure adding watch "..op.." -> ignored ")
		return
	end

	local ilist = inotifies[wd]
	if not ilist then
		ilist = new_array()
		inotifies[wd] = ilist
	end
	local inotify = { origin = origin, path = path } 
	set_prototype(inotify, proto_inotify)
	table.insert(ilist, inotify)

	-- on a warmstart add a Create for the directory
	if not origin.config.startup then
		delay_action("Create", wd, sync, nil, nil, nil)
	end

	-- registers and adds watches for all subdirectories 
	local subdirs = lsyncd.sub_dirs(op)
	for _, dirname in ipairs(subdirs) do
		inotify_watch_dir(origin, path..dirname.."/")
	end
end

-----
-- Called from code whenever a child process finished and 
-- zombie process was collected by core.
--
function lsyncd_collect_process(pid, exitcode) 
	local delay = nil
	local origin = nil
	for _, o in ipairs(origins) do
		delay = o.processes[pid]
		if delay then
			origin = o
			break
		end
	end
	if not delay then
		return
	end
	log(DEBUG, "collected "..pid..": "..
		delay.ename.." of "..
		origin.source..delay.pathname..
		" = "..exitcode)
	origin.processes[pid] = nil
end

------
-- Hidden key for lsyncd.lua internal variables not ment for
-- the user to see
--
local hk = {}

------
--- TODO
local inlet = {
	[hk] = {
		origin  = true,
		delay   = true,
	},

	config = function(self)
		return self[hk].origin.config
	end,

	nextevent = function(self)
		local h = self[hk]
		return { 
			spath  = h.origin.source .. h.delay.pathname,
			tpath  = h.origin.targetident .. h.delay.pathname,
			ename  = h.delay.ename
		} 
	end,
}


-----
-- TODO
--
--
local function invoke_action(origin, delay)
	local o       = origin
	local config  = o.config
	if delay.ename == "None" then
		-- a removed action
		return
	end
	
	inlet[hk].origin = origin
	inlet[hk].delay  = delay
	local pid = config.action(inlet)
	if pid and pid > 0 then
		o.processes[pid] = delay
	end
end
	

----
-- Called from core to get a status report written into a file descriptor
--
function lsyncd_status_report(fd)
	local w = lsyncd.writefd
	w(fd, "Lsyncd status report at "..os.date().."\n\n")
	w(fd, "Watching "..inotifies.size.." directories\n")
	for wd, v in pairs(inotifies.nt) do
		w(fd, "  "..wd..": ")
		for _, inotify in ipairs(v) do 
			w(fd, "("..inotify.origin.source.."|"..(inotify.path) or ")..")
		end
		w(fd, "\n")
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
		if o.processes.size < o.config.max_processes then
			local delays = o.delays
			local d = delays[1]
			if d and lsyncd.before_eq(d.alarm, now) then
				invoke_action(o, d)
				table.remove(delays, 1)
				o.delayname[d.pathname] = nil -- TODO grab from stack
			end
		end
	end
end


-----
-- Called by core before anything is "-help" or "--help" is in
-- the arguments.
--
function lsyncd_help()
	io.stdout:write(
[[TODO this is a multiline
help
]])
	os.exit(-1) -- ERRNO
end


----
-- Called from core on init or restart after user configuration.
-- 
function lsyncd_initialize(args)
	-- creates settings if user didnt
	settings = settings or {}

	-- From this point on, no globals may be created anymore
	globals_lock()

	-- parses all arguments
	for i = 1, #args do
		local a = args[i]
		if a:sub(1, 1) ~= "-" then
			log(ERROR, "Unknown option "..a..
				". Options must start with '-' or '--'.")
			os.exit(-1) -- ERRNO
		end
		if a:sub(1, 2) == "--" then
			a = a:sub(3)
		else
			a = a:sub(2)
		end
		--TODO
	end

	-- all valid settings, first value is 1 if it needs a parameter 
	local configure_settings = {
		loglevel = {1, 
			function(param)
				if not (param == DEBUG or param == NORMAL or
				        param == VERBOSE or param == ERROR) then
					log(ERROR, "unknown settings.loglevel '"..param.."'")
					terminate(-1); -- ERRNO
				end
			end},
		statusfile = {1, nil},
	}

	-- check all entries in the settings table
	for c, p in pairs(settings) do
		local cs = configure_settings[c]
		if not cs then
			log(ERROR, "unknown setting '"..c.."'")	
			terminate(-1) -- ERRNO
		end
		if cs[1] == 1 and not p then
			log(ERROR, "setting '"..c.."' needs a parameter")	
		end
		-- calls the check function if its not nil
		if cs[2] then
			cs[2](p)
		end
		lsyncd.configure(c, p)
	end

	-- makes sure the user gave lsyncd anything to do 
	if #origins == 0 then
		log(ERROR, "Nothing to watch!")
		log(ERROR, "Use sync(SOURCE, TARGET, BEHAVIOR) in your config file.");
		terminate(-1) -- ERRNO
	end


	-- set to true if at least one origin has a startup function
	local have_startup = false
	-- runs through the origins table filled by user calling directory()
	for _, o in ipairs(origins) do
		-- resolves source to be an absolute path
		local asrc = lsyncd.real_dir(o.source)
		local config = o.config
		if not asrc then
			log(Error, "Cannot resolve source path: " .. o.source)
			terminate(-1) -- ERRNO
		end
		o.source = asrc
		o.delays = new_count_array()
		o.delayname = {}
		o.processes = new_count_array()

		config.max_processes = 
			config.max_processes or 
			settings.max_processes or 
			defaults.max_processes

		config.collapse_table =
			config.collapse_table or
			settings.collapse_table or 
			defaults.collapse_table

		if config.startup then
			have_startup = true
		end
		-- adds the dir watch inclusively all subdirs
		inotify_watch_dir(o, "")
	end

	-- from this point on use logging facilities as configured.
	lsyncd.configure("running");
	
	if have_startup then
		log(NORMAL, "--- startup ---")
		local pids = { }
		for _, o in ipairs(origins) do
			local pid
			if o.config.startup then
				local pid = o.config.startup(o.source, o.targetident)
				table.insert(pids, pid)
			end
		end
		lsyncd.wait_pids(pids, "startup_collector")
		log(NORMAL, "--- Entering normal operation with "..
			inotifies.size.." monitored directories ---")
	else
		log(NORMAL, "--- Warmstart into normal operation with "..
			inotifies.size.." monitored directories ---")
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
		if o.delays[1] and 
		   o.processes.size < o.config.max_processes then
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
-- @param ename     "Attrib", "Mofify", "Create", "Delete", "Move")
-- @param wd        watch descriptor (matches lsyncd.add_watch())
-- @param time      time of event
-- @param filename  string filename without path
-- @param filename2 
--
function lsyncd_event(ename, wd, isdir, time, filename, filename2)
	local ftype;
	if isdir then
		ftype = "directory"
	else
		ftype = "file"
	end
	-- TODO comment out to safe performance
	if filename2 then
		log(DEBUG, "got event "..ename..
			" of "..ftype.." "..filename.." to "..filename2) 
	else 
		log(DEBUG, "got event "..ename..
			" of "..ftype.." "..filename) 
	end

	-- looks up the watch descriptor id
	local ilist = inotifies[wd]
	if not ilist then
		log(NORMAL, "event belongs to unknown or deleted watch descriptor.")
		return
	end
	
	-- works through all possible source->target pairs
	for _, inotify in ipairs(ilist) do
		local pathname2 
		if filename2 then
			pathname2 = inotify.path..filename2
		end
		delay_action(ename, wd, time, inotify.origin, 
			inotify.path..filename, pathname2)
		-- add subdirs for new directories
		if isdir then
			if ename == "Create" then
				inotify_watch_dir(inotify.origin, 
					inotify.path .. filename .. "/")
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
		terminate(-1) -- ERRNO
	end
	return 0
end


--============================================================================
-- lsyncd user interface
--============================================================================

----
-- Adds one directory (incl. subdirs) to be synchronized.
-- Users primary configuration device.
--
-- @param TODO
--
function sync(source_dir, target_identifier, config)
	local o = {      config = config, 
	                 source = source_dir, 
	            targetident = target_identifier, 
	}
	set_prototype(o, proto_origin)

	if not config.max_actions then
		config.max_actions = 1  -- TODO move to init
	end
	table.insert(origins, o)
	return 
end

----
-- Called by core when an overflow happened.
--
function default_overflow()
	log(ERROR, "--- OVERFLOW on inotify event queue ---")
	terminate(-1) -- TODO reset instead.
end
overflow = default_overflow

-----
-- Spawns a child process using bash.
--
function shell(command, ...)
	return exec("/bin/sh", "-c", command, "/bin/sh", ...)
end

--============================================================================
-- lsyncd default settings
--============================================================================

-----
-- lsyncd classic - sync with rsync
--
local default_rsync = {
	----
	-- Called for every sync/target pair on startup
	startup = function(source, target) 
		log(NORMAL, "startup recursive rsync: "..source.." -> "..target)
		return exec("/usr/bin/rsync", "-ltrs", 
			source, target)
	end,

	default = function(source, target, path)
		return exec("/usr/bin/rsync", "--delete", "-ltds",
			source.."/".. path, target .. "/" .. path)
	end
}

-----
-- The defaults table for the user to access 
--
defaults = {
	-----
	-- TODO
	--
	max_processes = 1,

	------
	-- TODO
	--
	collapse_table = {
		Attrib = { Attrib = "Attrib", Modify = "Modify", Create = "Create", Delete = "Delete" },
		Modify = { Attrib = "Modify", Modify = "Modify", Create = "Create", Delete = "Delete" },
		Create = { Attrib = "Create", Modify = "Create", Create = "Create", Delete = -1       },
		Delete = { Attrib = "Delete", Modify = "Delete", Create = "Modify", Delete = "Delete" },
	},

	rsync = default_rsync
}


