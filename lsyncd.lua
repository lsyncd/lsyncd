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

-----
-- A security measurement.
-- Core will exit if version ids mismatch.
--
if lsyncd_version then
	-- checks if the runner is being loaded twice 
	lsyncd.log("Error",
		"You cannot use the lsyncd runner as configuration file!")
	lsyncd.terminate(-1) -- ERRNO
end
lsyncd_version = "2.0beta1"

-----
-- Hides the core interface from user scripts
--
_l = lsyncd
lsyncd = nil
local lsyncd = _l
_l = nil

-----
-- Shortcuts (which user is supposed to be able to use them as well)
--
log  = lsyncd.log
terminate = lsyncd.terminate

--============================================================================
-- Lsyncd Prototypes 
--============================================================================

-----
-- The array objects are tables that error if accessed with a non-number.
--
local Array = (function()
	-- Metatable
	local mt = {}

	-- on accessing a nil index.
	mt.__index = function(t, k) 
		if type(k) ~= "number" then
			error("Key '"..k.."' invalid for Array", 2)
		end
		return rawget(t, k)
	end

	-- on assigning a new index.
	mt.__newindex = function(t, k, v)
		if type(k) ~= "number" then
			error("Key '"..k.."' invalid for Array", 2)
		end
		rawset(t, k, v)
	end

	-- creates a new object
	local function new()
		local o = {}
		setmetatable(o, mt)
		return o
	end

	-- objects public interface
	return {new = new}
end)()


-----
-- The count array objects are tables that error if accessed with a non-number.
-- Additionally they maintain their length as "size" attribute.
-- Lua's # operator does not work on tables which key values are not 
-- strictly linear.
--
local CountArray = (function()
	-- Metatable
	local mt = {}

	-----
	-- key to native table
	local k_nt = {}
	
	-----
	-- on accessing a nil index.
	mt.__index = function(t, k) 
		if type(k) ~= "number" then
			error("Key '"..k.."' invalid for CountArray", 2)
		end
		return t[k_nt][k]
	end

	-----
	-- on assigning a new index.
	mt.__newindex = function(t, k, v)
		if type(k) ~= "number" then
			error("Key '"..k.."' invalid for CountArray", 2)
		end
		-- value before
		local vb = t[k_nt][k]
		if v and not vb then
			t._size = t._size + 1
		elseif not v and vb then
			t._size = t._size - 1
		end
		t[k_nt][k] = v
	end

	-----
	-- Walks through all entries in any order.
	--
	local function walk(self)
		return pairs(self[k_nt])
	end

	-----
	-- returns the count
	--
	local function size(self)
		return self._size
	end

	-----
	-- creates a new count array
	--
	local function new()
		-- k_nt is native table, private for this object.
		local o = {_size = 0, walk = walk, size = size, [k_nt] = {} }
		setmetatable(o, mt)
		return o
	end

	-----
	-- public interface
	--
	return {new = new}
end)()

----
-- Locks globals,
-- no more globals can be created
--
local function lockGlobals()
	local t = _G
	local mt = getmetatable(t) or {}
	mt.__index = function(t, k) 
		if (k~="_" and string.sub(k, 1, 2) ~= "__") then
			error("Access of non-existing global '"..k.."'", 2)
		else
			rawget(t, k)
		end
	end
	mt.__newindex = function(t, k, v) 
		if (k~="_" and string.sub(k, 1, 2) ~= "__") then
			error("Lsyncd does not allow GLOBALS to be created on the fly. " ..
			      "Declare '" ..k.."' local or declare global on load.", 2)
		else
			rawset(t, k, v)
		end
	end
	setmetatable(t, mt)
end

-----
-- Holds information about a delayed event for one Sync.
--
-- valid stati are:
--    delay
--    active
--    TODO
--
local Delay = (function()
	-----
	-- Creates a new delay.
	-- 
	-- @param TODO
	local function new(etype, alarm, path, path2)
		local o = {
			etype = etype,
			alarm = alarm,
			path  = path,
			path2  = path2,
			status = "wait",
		}
		return o
	end

	return {new = new}
end)()

-----
-- User interface to grap events
--
-- InletControl is the Luas runner part to control the interface
-- hidden from the user.
--
local Inlet, InletControl = (function()
	-- lua runner controlled variables
	local sync 

	-- key variables for delays hidden from user
	local delayKey = {}

	-- nil for non move events
	-- 0 for move source events
	-- 1 for move desination events
	local moveDestKey = {}

	-----
	-- removes the trailing slash from a path
	local function cutSlash(path) 
		if string.byte(path, -1) == 47 then
			return string.sub(path, 1, -2)
		else
			return path
		end
	end

	local function getPath(event)
		if not event[moveDestKey] then
			return event[delayKey].path
		else
			return event[delayKey].path2
		end
	end

	-----
	-- Interface for the user to get fields.
	local eventFields = {
		config = function(event)
			return event[delayKey].sync.config
		end,

		-----
		-- Returns the type of the event.
		-- Can be:
		--    "Attrib"
		--    "Create"
		--    "Delete"
		--    "Modify"
		--    "Move"
		etype = function(event)
			return event[delayKey].etype
		end,
		
		-----
		-- Returns true if event relates to a directory.
		isdir = function(event) 
			return string.byte(getPath(event), -1) == 47
		end,

		-----
		-- Returns the name of the file/dir.
		-- Includes a trailing slash for dirs.
		name = function(event)
			return string.match(getPath(event), "[^/]+/?$")
		end,
		
		-----
		-- Returns the name of the file/dir.
		-- Excludes a trailing slash for dirs.
		basename = function(event)
			return string.match(getPath(event), "([^/]+)/?$")
		end,

		-----
		-- Returns the file/dir relative to watch root
		-- Includes a trailing slash for dirs.
		path = function(event)
			return getPath(event)
		end,
		
		-----
		-- Returns the file/dir relativ to watch root
		-- Excludes a trailing slash for dirs.
		pathname = function(event)
			return cutSlash(getPath(event))
		end,
		
		------
		-- Returns the absolute path of the watch root.
		-- All symlinks will have been resolved.
		source = function(event)
			return sync.source
		end,

		------
		-- Returns the absolute path of the file/dir.
		-- Includes a trailing slash for dirs.
		sourcePath = function(event)
			return sync.source ..getPath(event)
		end,
		
		------
		-- Returns the absolute path of the file/dir.
		-- Excludes a trailing slash for dirs.
		sourcePathname = function(event)
			return sync.source .. cutSlash(getPath(event))
		end,
		
		------
		-- Returns the target. 
		-- Just for user comfort, for most case
		-- (Actually except of here, the lsyncd.runner itself 
		--  does not care event about the existance of "target",
		--  this is completly up to the action scripts.)
		target = function(event)
			return sync.config.target
		end,

		------
		-- Returns the relative dir/file appended to the target.
		-- Includes a trailing slash for dirs.
		targetPath = function(event)
			return sync.config.target .. getPath(event)
		end,
		
		------
		-- Returns the relative dir/file appended to the target.
		-- Excludes a trailing slash for dirs.
		targetPathname = function(event)
			return sync.config.target .. cutSlash(getPath(event))
		end,
	}

	-----
	-- Calls event functions for the user.
	local eventMeta = {
		__index = function(t, k)
			local f = eventFields[k]
			if not f then
				if k == moveDestKey then
					-- possibly undefined
					return nil
				end
				error("event does not have field '"..k.."'", 2)
			end
			return f(t)
		end
	}
	
	-----
	-- Encapsulates a delay to an event for the user
	--
	-- TODO this hidden key technique can be circumvented with 
	-- pairs(), use a weak table as referencer instead.
	--
	local function toEvent(delay)
		if not delay.event then
			if delay.etype ~= "Move" then
				delay.event = {}
				setmetatable(delay.event, eventMeta)
				delay.event[delayKey] = delay
			end
				delay.event = {}
				delay.event2 = {}
				setmetatable(delay.event, eventMeta)
				setmetatable(delay.event2, eventMeta)
				delay.event[delayKey] = delay
				delay.event2[delayKey] = delay
		end
		return delay.event, delay.event2
	end

	
	-----
	-- Creates a blanketEvent that blocks everything
	-- and is blocked by everything.
	--
	local function createBlanketEvent()
		return toEvent(sync:addBlanketDelay())
	end

	-----
	-- Gets the next event from queue.
	--
	local function getEvent()
		return toEvent(sync:getNextDelay(lysncd.now()))
	end

	-----
	-- Returns the configuration table specified by sync{}
	--
	local function getConfig()
		-- TODO give a readonly handler only.
		return sync.config
	end

	-----
	-- Interface for lsyncd runner to control what
	-- the inlet will present the user.
	--
	local function setSync(setSync)
		sync = setSync
	end

	-----
	-- Return the inner config
	--    not to be called from user
	local function getInterior(event)
		return sync, event[delayKey]
	end

	-----
	-- public interface.
	-- this one is split, one for user one for runner.
	return {
			getEvent = getEvent, 
			getConfig = getConfig, 
			createBlanketEvent = createBlanketEvent,
		}, {
			setSync = setSync, 
			getInterior = getInterior, -- TODO <- remove
			toEvent = toEvent,
		}
end)()

-----
-- Holds information about one observed directory inclusively subdirs.
--
local Sync = (function()

	-----
	-- Syncs that have no name specified get an incremental default name
	--
	local nextDefaultName = 1
	
	-----
	-- Collects a child process 
	--
	local function collect(self, pid, exitcode)
		local delay = self.processes[pid]
		if not delay then
			-- not a child of this sync.
			return
		end
		if delay.status ~= "active" then
			error("internal fail, collecting a non-active process")
		end
		-- TODO call user collector
		local found
		for i, d in ipairs(self.delays) do
			if d == delay then
				found = true
				table.remove(self.delays, i)
				break
			end
		end
		if not found then
			error("Did not find a delay!")
		end
		log("Normal","Return of ",delay.etype," on ",
			self.source,delay.path," = ",exitcode)
		self.processes[pid] = nil
	end

	-----
	-- Puts an action on the delay stack.
	--
	local function delay(self, etype, time, path, path2)
		log("Function", "delay(", self.config.name,", ",etype,", ",path,")")
		if path2 then
			log("Function", "...delay(+",path2,")")
		end

		if etype == "Move" and not self.config.onMove then
			-- if there is no move action defined, split a move as delete/create
			log("Debug", "splitting Move into Delete & Create")
			delay(self, "Delete", time, path,  nil)
			delay(self, "Create", time, path2, nil)
			return
		end

		-- creates the new action
		local alarm 
		if time and self.config.delay then
			alarm = lsyncd.addtoclock(time, self.config.delay)
		else
			alarm = lsyncd.now()
		end
		-- new delay
		local nd = Delay.new(etype, alarm, path, path2)
		if nd.etype == "Move" then
			log("Normal", "Stacking a move event ",path," -> ",path2)
			table.insert(self.delays, nd)
		end

		-----
		-- detects blocks and collapses by working from back til 
		-- front through the fifo
		InletControl.setSync(self)
		local ne = InletControl.toEvent(nd)
		local il = #self.delays -- last delay

		-----
		-- TODO
		--
		local function doCollapse(oe, ne)
		end

		while il > 0 do
			local od = self.delays[il]
			-- tries to collapse identical paths
			local oe, oe2 = InletControl.toEvent(od) 
			local ne = InletControl.toEvent(nd) -- TODO more logic on moves

			local oel = oe
			-- this mini loop repeats the collapse a second time for move 
			-- events
			while oel do
				local c = self.config.collapse(oel, ne, self.config)
				if c == 0 then
					-- events nullificate each ether
					od.etype = "None"  -- TODO better remove?
					return "return"
				elseif c == 1 then
					log("Normal", nd.etype, " is absored by event ",
						od.etype, " on ", path)
					return "return"
				elseif c == 2 then
					log("Normal", nd.etype, " replaces event ",
						od.etype, " on ", path)
					self.delays[il] = nd
					return "return"
				elseif c == 3 then
					log("Normal", "Stacking ", nd.etype, " upon ",
						od.etype, " on ", path)
					return "break"
				end	
				if oel == oe2 then
					oel = false
				else
					oel = oe2
				end
			end
			il = il - 1
		end
		if il <= 0 then
				log("Normal", "Stacking ", nd.etype, " upon on ", path)
		end
		-- there was no hit on collapse or it decided to stack.
		table.insert(self.delays, nd)
	end
		
	-----	
	-- Returns the nearest alarm for this Sync.
	--
	local function getAlarm(self)
		-- first checks if more processses could be spawned 
		if self.processes:size() >= self.config.maxProcesses then
			return nil
		end

		if self.delays[1] then 
			return self.delays[1].alarm
		end
	end

	-----
	-- Gets the next event to be processed.
	--
	local function getNextDelay(self, now)
		for i, d in ipairs(self.delays) do
			if d.alarm ~= true and lsyncd.clockbefore(now, d.alarm) then
				-- reached point in stack where delays are in future
				return nil
			end
			if d.status == "wait" then
				-- found a waiting delay
				return d
			end
		end
	end

	-----
	-- Creates new actions
	--
	local function invokeActions(self, now)
		log("Function", "invokeActions('",self.config.name,"',",now,")")
		if self.processes:size() >= self.config.maxProcesses then
			-- no new processes
			return
		end
		for _, d in ipairs(self.delays) do
			if d.alarm ~= true and lsyncd.clockbefore(now, d.alarm) then
				-- reached point in stack where delays are in future
				return
			end
			if d.status == "wait" then
				-- found a waiting delay
				InletControl.setSync(self)
				self.config.action(Inlet)
				if self.processes:size() >= self.config.maxProcesses then
					-- no further processes
					return
				end
			end
		end
	end


	------
	-- adds and returns a blanket delay thats blocks all 
	-- (used in startup)
	--
	local function addBlanketDelay(self)
		local newd = Delay.new("Blanket", true, "/")
		table.insert(self.delays, newd)
		return newd 
	end

	-----
	-- Creates a new Sync
	--
	local function new(config) 
		local s = {
			-- fields
			config = config,
			delays = CountArray.new(),
			source = config.source,
			processes = CountArray.new(),

			-- functions
			collect         = collect,
			delay           = delay,
			addBlanketDelay = addBlanketDelay,
			getAlarm        = getAlarm,
			getNextDelay    = getNextDelay,
			invokeActions   = invokeActions,
		}
		-- provides a default name if needed
		if not config.name then
			config.name = "Sync" .. nextDefaultName
		end
		-- increments default nevertheless to cause less confusion
		-- so name will be the n-th call to sync{}
		nextDefaultName = nextDefaultName + 1
		return s
	end

	-----
	-- public interface
	--
	return {new = new}
end)()


-----
-- Syncs - a singleton
-- 
-- It maintains all configured directories to be synced.
--
local Syncs = (function()
	-----
	-- the list of all syncs
	--
	local list = Array.new()
	
	-----
	-- inheritly copies all non integer keys from
	-- @cd copy destination
	-- to
	-- @cs copy source
	-- all integer keys are treated as new copy sources
	--
	local function inherit(cd, cs)
		-- first copies from source all 
		-- non-defined non-integer keyed values 
		for k, v in pairs(cs) do
			if type(k) ~= "number" and not cd[k] then
				cd[k] = v
			end
		end
		-- first recurses into all integer keyed tables
		for i, v in ipairs(cs) do
			if type(v) == "table" then
				inherit(cd, v)
			end
		end
	end
	
	-----
	-- Adds a new directory to observe.
	--
	local function add(config)
		-----
		-- Creates a new config table and inherit all keys/values
		-- from integer keyed tables
		--
		local uconfig = config
		config = {}
		inherit(config, uconfig)

		-----
		-- raises an error if @param name isnt in opts
		local function require_opt(name)
			if not config[name] then
				local info = debug.getinfo(3, "Sl")
				log("Error", info.short_src, ":", info.currentline,
					": ", name, " missing from sync.")
				terminate(-1) -- ERRNO
			end
		end
		require_opt("source")

		-- absolute path of source
		local real_src = lsyncd.realdir(config.source)
		if not real_src then
			log("Error", "Cannot access source directory: ",config.source)
			terminate(-1) -- ERRNO
		end
		config._source = config.source
		config.source = real_src

		if not config.action   and not config.onAttrib and
		   not config.onCreate and not config.onModify and
		   not config.onDelete and not config.onMove
		then
			local info = debug.getinfo(3, "Sl")
			log("Error", info.short_src, ":", info.currentline,
				": no actions specified, use e.g. 'config=default.rsync'.")
			terminate(-1) -- ERRNO
		end

		-- loads a default value for an option if not existent
		for dn, dv in pairs(default) do
			if config[dn] == nil then
				config[dn] = settings[dn] or dv
			end
		end

		--- creates the new sync
		local s = Sync.new(config)
		table.insert(list, s)
	end

	-----
	-- allows to walk through all syncs
	--
	local function iwalk()
		return ipairs(list)
	end

	-----
	-- returns the number of syncs
	--
	local size = function()
		return #list
	end

	-- public interface
	return {add = add, iwalk = iwalk, size = size}
end)()

-----
-- Interface to inotify, watches recursively subdirs and 
-- sends events.
--
-- All inotify specific implementation should be enclosed here.
-- So lsyncd can work with other notifications mechanisms just
-- by changing this.
--
local Inotifies = (function()
	-----
	-- A list indexed by inotifies watch descriptor.
	-- Contains a list of all syncs observing this directory
	-- (directly or by recurse)
	local wdlist = CountArray.new()

	-----
	-- A list indexed by sync's containing a list of all paths
	-- watches by this sync pointing to the watch descriptor.
	local syncpaths = {}

	-----
	-- Adds watches for a directory including all subdirectories.
	--
	-- @param root+path  directory to observe
	-- @param recurse    true if recursing into subdirs or 
	--                   the relative path to root for recursed inotifies
	-- @param sync       link to the observer to be notified.
	--                   Note: Inotifies should handle this opaquely
	local function add(root, path, recurse, sync)
		log("Function", 
			"Inotifies.add(",root,", ",path,", ",recurse,", ",sync,")")
		-- registers watch 
		local wd = lsyncd.inotifyadd(root .. path);
		if wd < 0 then
			log("Error","Failure adding watch ",dir," -> ignored ")
			return
		end

		if not wdlist[wd] then
			wdlist[wd] = Array.new()
		end
		table.insert(wdlist[wd], {
			root = root,
			path = path,
			recurse = recurse,
			sync = sync
		})
		-- create an entry for receival of with sync/path keys
		local sp = syncpaths[sync]
		if not sp then
			sp = {}
			syncpaths[sync] = sp
		end
		sp[path] = wd

		-- registers and adds watches for all subdirectories 
		if recurse then
			local subdirs = lsyncd.subdirs(root .. path)
			for _, dirname in ipairs(subdirs) do
				add(root, path..dirname.."/", true, sync)
			end
		end
	end

	-----
	-- Removes one event receiver from a directory.
	--
	function removeSync(sync, path)
	    local sp = syncpaths[sync]
		if not sp then
			error("internal fail, removeSync, nonexisting syncpath-sync.")
		end
		local wd = sp[path]
		if not wd then
			error("internal fail, removeSync, nonexisting syncpath-wd.")
		end
		local ilist = wdlist[wd]
		if not ilist then
			error("internal fail, removeSync, nonexisting syncpath-ilist.")
		end
		-- TODO optimize for 1 entry only case
		local i, found
		for i, v in ipairs(ilist) do
			if v.sync == sync then
				found = true
				break
			end
		end
		if not found then
			error("internal fail, removeSync, nonexisiting syncpath-i.")
		end
		table.remove(ilist, i)
		if #ilist == 0 then
			wdlist[wd] = nil
			lsyncd.inotifyrm(wd)
		end
		sp[path] = nil
	end

	-----
	-- Called when an event has occured.
	--
	-- @param etype     "Attrib", "Mofify", "Create", "Delete", "Move")
	-- @param wd        watch descriptor (matches lsyncd.add_watch())
	-- @param isdir     true if filename is a directory
	-- @param time      time of event
	-- @param filename  string filename without path
	-- @param filename2 
	--
	function event(etype, wd, isdir, time, filename, filename2)
		local ftype;
		if isdir then
			ftype = "directory"
			filename = filename .. "/"
			if filename2 then
				filename2 = filename2 .. "/"
			end
		end
		if filename2 then
			log("Inotify", "got event ", etype, " ", filename, 
				" to ", filename2) 
		else 
			log("Inotify", "got event ", etype, " ", filename) 
		end

		local ilist = wdlist[wd]
		-- looks up the watch descriptor id
		if not ilist then
			-- this is normal in case of deleted subdirs
			log("Inotify", "event belongs to unknown watch descriptor.")
			return
		end
	
		-- works through all observers interested in this directory
		for _, inotify in ipairs(ilist) do
			local path = inotify.path .. filename
			local path2 
			if filename2 then
				path2 = inotify.path .. filename2
			end
			inotify.sync:delay(etype, time, path, path2)
			-- adds subdirs for new directories
			if isdir and inotify.recurse then
				if etype == "Create" then
					add(inotify.root, path, true, inotify.sync)
				elseif etype == "Delete" then
					removeSync(inotify.sync, path)
				elseif etype == "Move" then
					removeSync(inotify.sync. path)
					add(inotify.root, path2, true, inotify.sync)
				end
			end
		end
	end

	-----
	-- Writes a status report about inotifies to a filedescriptor
	--
	local function status_report(f)
		f:write("Watching ",wdlist:size()," directories\n")
		for wd, v in wdlist:walk() do
			f:write("  ",wd,": ")
			local sep = ""
			for _, v in ipairs(v) do
				f:write(v.root,"/",v.path or "",sep)
				sep = ", "
			end
			f:write("\n")
		end
	end

	-----
	-- Returns the number of directories watched in total.
	local function size()
		return wdlist:size()
	end

	-- public interface
	return { 
		add = add, 
		size = size, 
		event = event, 
		status_report = status_report 
	}
end)()

--============================================================================
-- lsyncd runner plugs. These functions will be called from core. 
--============================================================================

-----
-- true after lsyncd_initalized()
--
local running = false

-----
-- Called from core whenever a lua failed.
--
function lsyncd_call_error(message)
	log("Error", "IN LUA: ", message)
	-- prints backtrace
	local level = 2
	while true do
		local info = debug.getinfo(level, "Sl")
		if not info then
			terminate(-1) -- ERRNO
		end
		log("Error", "Backtrace ", level - 1, " :", 
			info.short_src, ":", info.currentline)
		level = level + 1
	end
end

-----
-- Called from code whenever a child process finished and 
-- zombie process was collected by core.
--
function lsyncd_collect_process(pid, exitcode) 
	for _, s in Syncs.iwalk() do
		if s:collect(pid, exitcode) then
			return
		end
	end
end


----
-- Writes a status report file at most every [statusintervall] seconds.
--
--
local StatusFile = (function() 
	-----
	-- Timestamp when the status file has been written.
	local lastWritten = false

	-----
	-- Timestamp when a statusfile should be written
	local alarm = false

	-----
	-- Returns when the statusfile should be written
	--
	local function getAlarm()
		return alarm
	end

	-----
	-- Called to check if to write a status file.
	--
	local function write(now)
		log("Function", "write(", now, ")")

		-- some logic to not write too often
		if settings.statusIntervall > 0 then
			-- already waiting
			if alarm and lsyncd.clockbefore(now, alarm) then
				log("Statusfile", "waiting(",now," < ",alarm,")")
				return
			end
			-- determines when a next write will be possible
			if not alarm then
				local nextWrite = lastWritten and
					lsyncd.addtoclock(now, settings.statusIntervall)
				if nextWrite and lsyncd.clockbefore(now, nextWrite) then
					log("Statusfile", "setting alarm: ", nextWrite)
					alarm = nextWrite
					return
				end
			end
			lastWritten = now
			alarm = false
		end

		log("Statusfile", "writing now")
		local f, err = io.open(settings.statusfile, "w")
		if not f then
			log("Error", "Cannot open statusfile '"..settings.statusfile..
				"' :"..err)
			return
		end
		f:write("Lsyncd status report at ", os.date(), "\n\n")
		Inotifies.status_report(f)
		f:close()
	end

	-- public interface
	return {write = write, getAlarm = getAlarm}
end)()

----
-- Called from core everytime a masterloop cycle runs through.
-- This happens in case of 
--   * an expired alarm.
--   * a returned child process.
--   * received inotify events.
--   * received a HUP or TERM signal.
--
-- @param now   the current kernel time (in jiffies)
--
function lsyncd_cycle(now)
	-- goes through all syncs and spawns more actions
	-- if possible
	for _, s in Syncs.iwalk() do
		s:invokeActions(now)
	end

	if settings.statusfile then
		StatusFile.write(now)
	end
	log("Debug", "fin lsyncd_cycle")
end


-----
-- Called by core before anything is "-help" or "--help" is in
-- the arguments.
--
function lsyncd_help()
	io.stdout:write(
[[
USAGE: 
  run a config file:
    lsyncd [OPTIONS] [CONFIG-FILE]

  default rsync behaviour:
    lsyncd [OPTIONS] -rsync [SOURCE] [TARGET1]  [TARGET2] ...

OPTIONS:
  -help               Shows this
  -log    all         Logs everything
  -log    scarce      Logs errors only
  -log    [Category]  Turns on logging for a debug category
  -runner FILE        Loads lsyncds lua part from FILE 

LICENSE:
  GPLv2 or any later version.

SEE:
  `man lsyncd` for further information.

]])
	os.exit(-1) -- ERRNO
end


-----
-- Called from core to parse the command line arguments
-- @returns a string as user script to load.
--          or simply 'true' if running with rsync bevaiour
-- terminates on invalid arguments
--
function lsyncd_configure(args)
	-- a list of all valid --options
	local options = {
		-- log is handled by core already.
		log = {1},

	}
	-- filled with all args that were non --options
	local nonopts = {}
	local i = 1
	while i <= #args do
		local a = args[i]
		if a:sub(1, 1) ~= "-" then
			table.insert(nonopts, args[i])
		else
			if a:sub(1, 2) == "--" then
				a = a:sub(3)
			else
				a = a:sub(2)
			end
			local o = options[a]
			if o then
				-- TODO --
				i = i + o[1]
			end
		end
		i = i + 1
	end

	if #nonopts == 0 then
		lsyncd_help(args[0])
	elseif #nonopts == 1 then
		return nonopts[1]
	else 
		-- TODO
		return true
	end
end


----
-- Called from core on init or restart after user configuration.
-- 
function lsyncd_initialize()
	-- creates settings if user didnt
	settings = settings or {}

	-- From this point on, no globals may be created anymore
	lockGlobals()

	-- TODO
	if settings.statusIntervall == nil then
		settings.statusIntervall = default.statusIntervall
	end

	-- makes sure the user gave lsyncd anything to do 
	if Syncs.size() == 0 then
		log("Error", "Nothing to watch!")
		log("Error", "Use sync(SOURCE, TARGET, BEHAVIOR) in your config file.");
		terminate(-1) -- ERRNO
	end

	-- from now on use logging as configured instead of stdout/err.
	running = true;
	lsyncd.configure("running");

	-- runs through the syncs table filled by user calling directory()
	for _, s in Syncs.iwalk() do
		Inotifies.add(s.source, "", true, s)
		if s.config.init then
			InletControl.setSync(s)
			s.config.init(Inlet)
		end
	end
end

----
-- Called by core to query soonest alarm.
--
-- @return false ... no alarm, core can in untimed sleep, or
--         true  ... immediate action
--         times ... the alarm time (only read if number is 1)
--
function lsyncd_get_alarm()
	local alarm = false

	----
	-- checks if current nearest alarm or a is earlier
	--
	local function checkAlarm(a) 
		if alarm == true or not a then
			-- already immediate or no new alarm
			return
		end
		if not alarm then
			alarm = a
		else
			alarm = lsyncd.earlier(alarm, a)
		end
	end

	-- checks all syncs for their earliest alarm
	for _, s in Syncs.iwalk() do
		checkAlarm(s:getAlarm())
	end
	-- checks if a statusfile write has been delayed
	checkAlarm(StatusFile.getAlarm())

	log("Debug", "lysncd_get_alarm returns: ",alarm)
	return alarm
end

lsyncd_inotify_event = Inotifies.event

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
		log("Error", "Startup process", pid, " failed")
		terminate(-1) -- ERRNO
	end
	return 0
end


--============================================================================
-- lsyncd user interface
--============================================================================

-----
-- Main utility to create new observations.
--
function sync(opts)
	if running then
		error("Cannot add new syncs while running!")
	end
	Syncs.add(opts)
end

----
-- Called by core when an overflow happened.
--
function default_overflow()
	log("Error", "--- OVERFLOW on inotify event queue ---")
	terminate(-1) -- TODO reset instead.
end
overflow = default_overflow

-----
-- Spawn a new child process
--
-- @param agent   the reason why a process is spawned.
--                normally this is a delay/event of a sync.
--                it will mark the related files as blocked.
--                or it is a string saying "all", that this 
--                process blocks all events and is blocked by all
--                this is used on startup.
-- @param collect a table of exitvalues and the action that shall taken.
-- @param binary  binary to call
-- @param ...     arguments
--
function spawn(agent, collector, binary, ...)
	local pid = lsyncd.exec(binary, ...)
	if pid and pid > 0 then
		local sync, delay = InletControl.getInterior(agent)
		delay.status = "active"
		delay.collector = collector
		sync.processes[pid] = delay
	end
end

-----
-- Spawns a child process using bash.
--
function spawnShell(agent, collector, command, ...)
	return spawn(agent, collector, "/bin/sh", "-c", command, "/bin/sh", ...)
end


-----
-- Comfort routine also for user.
-- Returns true if 'String' starts with 'Start'
--
function string.starts(String,Start)
	return string.sub(String,1,string.len(Start))==Start
end

-----
-- Comfort routine also for user.
-- Returns true if 'String' ends with 'End'
--
function string.ends(String,End)
	return End=='' or string.sub(String,-string.len(End))==End
end


--============================================================================
-- lsyncd default settings
--============================================================================

-----
-- lsyncd classic - sync with rsync
--
local defaultRsync = {
	-----
	-- Called for every sync/target pair on startup
	startup = function(source, config) 
		log("Normal", "startup recursive rsync: ", source, " -> ", target)
		return exec("/usr/bin/rsync", "-ltrs", source, target)
	end,

	default = function(inlet)
		-- TODO
		--return exec("/usr/bin/rsync", "--delete", "-ltds",
		--	source.."/"..path, target.."/"..path)
	end
}

-----
-- The default table for the user to access 
--   TODO make readonly
-- 
default = {
	-----
	-- Default action
	-- TODO desc
	--
	action = function(inlet)
		-- in case of moves getEvent returns the origin and dest of the move
		local event, event2 = inlet.getEvent()
		local config = inlet.getConfig()
		local func = config["on".. event.etype]
		if func then
			func(event, event2)
		end
	end,

	-----
	-- Called if to see if two events can be collapsed 
	-- @param event1    first event
	-- @param event2    second event
	-- @return -1  ... no interconnection
	--          0  ... drop both events.
	--          1  ... keep first event only
	--          2  ... keep second event only
	--          3  ... events block.
	--
	collapse = function(event1, event2, config)
		if event1.path == event2.path then
			if event1.etype == "Move" or event2.etype == "Move" then
				-- currently moves are always blocks
				return 3
			else
				-- asks the collapseTable what to do
				return config.collapseTable[event1.etype][event2.etype]
			end
		end
	
		-----
		-- Block events if one is a parent directory of another
		--
		if event1.isdir and string.start(event2.path, event1.path) then
			return 3
		end
		if event2.isdir and string.start(event1.path, event2.path) then
			return 3
		end

		return -1
	end,
	
	-----
	-- used by default collapse function
	--
	collapseTable = {
		Attrib = { Attrib = 1, Modify = 2, Create = 2, Delete = 2 },
		Modify = { Attrib = 1, Modify = 1, Create = 2, Delete = 2 },
		Create = { Attrib = 1, Modify = 1, Create = 1, Delete = 0 },
		Delete = { Attrib = 1, Modify = 1, Create = 3, Delete = 1 },
	},

	-----
	-- TODO
	--
	maxProcesses = 1,

	-----
	-- a default rsync configuration for easy usage.
	rsync = defaultRsync,

	-----
	-- Minimum seconds between two writes of a status file.
	--
	statusIntervall = 10,
}

