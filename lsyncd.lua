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
local _l = lsyncd
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
-- Holds information about a delayed event of one Sync.
--
local Delay = (function()
	-----
	-- Creates a new delay.
	-- 
	-- @params see below
	--
	local function new(etype, alarm, path, path2)
		local o = {
			-----
			-- Type of event.
			-- Can be 'Create', 'Modify', 'Attrib', 'Delete' and 'Move'
			etype = etype,

			-----
			-- Latest point in time this should be catered for.
			-- This value is in kernel ticks, return of the C's 
			-- times(NULL) call.
			alarm = alarm,

			-----
			-- path and filename or dirname of the delay relative 
			-- to the syncs root.
			-- for the directories it contains a trailing slash
			--
			path  = path,

			------
			-- only not nil for 'Move's.
			-- path and file/dirname of a move destination.
			--
			path2  = path2,
		
			------
			-- Status of the event. Valid stati are: 
			-- 'wait'    ... the event is ready to be handled.
			-- 'active'  ... there is process running catering for this event.
			-- 'blocked' ... this event waits for another to be handled first.
			-- 'done'    ... event has been collected. This should never be 
			--               visible as all references should be droped on
			--               collection, nevertheless seperat status for 
			--               insurrance.
			--
			status = "wait",
		}
		return o
	end

	return {new = new}
end)()

-----
-- User interface to grap events
--
-- InletControl is the runners part to control the interface
-- hidden from the user.
--
local Inlet, InletControl = (function()
	-- lua runner controlled variables
	local sync 

	-----
	-- table to receive the delay of an event.
	local e2d = {}
	-- doesnt stop the garbage collect to remove entries.
	setmetatable(e2d, { __mode = 'kv' })
	
	-- table to receive the delay list of an event list.
	local el2dl = {}
	-- doesnt stop the garbage collect to remove entries.
	setmetatable(el2dl, { __mode = 'kv' })

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
		if event.move ~= "To" then
			return e2d[event].path
		else
			return e2d[event].path2
		end
	end

	-----
	-- Interface for user scripts to get event fields.
	--
	local eventFields = {
		-----
		-- Returns a copy of the configuration as called by sync.
		-- But including all inherited data and default values.
		--
		-- TODO give user a readonly version.
		--
		config = function(event)
			return e2d[event].sync.config
		end,

		-----
		-- Returns the type of the event.
		-- Can be:
		--    "Attrib",
		--    "Create",
		--    "Delete",
		--    "Modify",
		--    "Move",
		--
		etype = function(event)
			return e2d[event].etype
		end,

		-----
		-- Tells script this isnt a list.
		--
		isList = function()
			return false
		end,

		-----
		-- Returns 'Fr'/'To' for events of moves.
		move = function(event)
			local d = e2d[event]
			if d.move then
				return d.move
			else 
				return ""
			end
		end,
	
		-----
		-- Status
		status = function(event)
			return e2d[event].status
		end,

		-----
		-- Returns true if event relates to a directory.
		--
		isdir = function(event) 
			return string.byte(getPath(event), -1) == 47
		end,

		-----
		-- Returns the name of the file/dir.
		-- Includes a trailing slash for dirs.
		--
		name = function(event)
			return string.match(getPath(event), "[^/]+/?$")
		end,
		
		-----
		-- Returns the name of the file/dir.
		-- Excludes a trailing slash for dirs.
		--
		basename = function(event)
			return string.match(getPath(event), "([^/]+)/?$")
		end,

		-----
		-- Returns the file/dir relative to watch root
		-- Includes a trailing slash for dirs.
		--
		path = function(event)
			return getPath(event)
		end,
		
		-----
		-- Returns the file/dir relativ to watch root
		-- Excludes a trailing slash for dirs.
		--
		pathname = function(event)
			return cutSlash(getPath(event))
		end,
		
		------
		-- Returns the absolute path of the watch root.
		-- All symlinks will have been resolved.
		--
		source = function(event)
			return sync.source
		end,

		------
		-- Returns the absolute path of the file/dir.
		-- Includes a trailing slash for dirs.
		--
		sourcePath = function(event)
			return sync.source .. getPath(event)
		end,
		
		------
		-- Returns the absolute path of the file/dir.
		-- Excludes a trailing slash for dirs.
		--
		sourcePathname = function(event)
			return sync.source .. cutSlash(getPath(event))
		end,
		
		------
		-- Returns the target. 
		-- Just for user comfort, for most case
		-- (Actually except of here, the lsyncd.runner itself 
		--  does not care event about the existance of "target",
		--  this is completly up to the action scripts.)
		--
		target = function(event)
			return sync.config.target
		end,

		------
		-- Returns the relative dir/file appended to the target.
		-- Includes a trailing slash for dirs.
		--
		targetPath = function(event)
			return sync.config.target .. getPath(event)
		end,
		
		------
		-- Returns the relative dir/file appended to the target.
		-- Excludes a trailing slash for dirs.
		--
		targetPathname = function(event)
			return sync.config.target .. cutSlash(getPath(event))
		end,
	}
	
	-----
	-- Retrievs event fields for the user script.
	--
	local eventMeta = {
		__index = function(t, k)
			local f = eventFields[k]
			if not f then
				if k == 'move' then
					-- possibly undefined
					return nil
				end
				error("event does not have field '"..k.."'", 2)
			end
			return f(t)
		end
	}
	
	-----
	-- Interface for user scripts to get event fields.
	--
	local eventListFuncs = {
		-----
		-- Returns the paths of all events.
		--
		getPaths = function(elist, delimiter)
			local dlist = el2dl[elist]
			if not dlist then
				error("cannot find delay list from event list.")
			end
			if not delimiter then
				delimiter = '\n'
			end
			local pl = {}
			local i = 1
			for k, d in pairs(dlist) do
				if type(k) == "number" then
					pl[i] = d.path
					i = i + 1
					if d.path2 then
						pl[i] = d.path2
						i = i + 1
					end
				end
			end
			return table.concat(pl, delimiter) .. delimiter
		end,
		
		-----
		-- Returns the absolute local paths of all events.
		--
		getSourcePaths = function(elist, delimiter)
			local dlist = el2dl[elist]
			if not dlist then
				error("cannot find delay list from event list.")
			end
			if not delimiter then
				delimiter = '\n'
			end
			local pl = {}
			local i = 1
			for k, d in pairs(dlist) do
				if type(k) == "number" then
					pl[i] = sync.source .. d.path
					i = i + 1
					if d.path2 then
						pl[i] = sync.source .. d.path2
						i = i + 1
					end
				end
			end
			return table.concat(pl, delimiter) .. delimiter
		end,
	}


	-----
	-- Retrievs event list fields for the user script.
	--
	local eventListMeta = {
		__index = function(t, k)
			if k == "isList" then
				return true
			end

			local f = eventListFuncs[k]
			if not f then
				error("event list does not have function '"..k.."'", 2)
			end
			
			return function()
				return f(t)
			end
		end
	}
	
	-----
	-- Encapsulates a delay into an event for the user script.
	--
	local function d2e(delay)
		if delay.etype ~= "Move" then
			if not delay.event then
				local event = {}
				delay.event = event
				setmetatable(event, eventMeta)
				e2d[event] = delay
			end
			return delay.event
		else
			-- moves have 2 events - origin and destination
			if not delay.event then
				local event  = {}
				local event2 = {}
				delay.event  = event
				delay.event2 = event2

				setmetatable(event, eventMeta)
				setmetatable(event2, eventMeta)
				e2d[delay.event] = delay
				e2d[delay.event2] = delay
				
				-- move events have a field 'event'
				event.move  = "Fr"
				event2.move = "To"
			end
			return delay.event, delay.event2
		end
	end
	
	-----
	-- Encapsulates a delay list into an event list for the user script.
	--
	local function dl2el(dlist)
		if not dlist.elist then
			local elist = {}
			dlist.elist = elist
			setmetatable(elist, eventListMeta)
			el2dl[elist] = dlist
		end
		return dlist.elist
	end

	
	-----
	-- Creates a blanketEvent that blocks everything
	-- and is blocked by everything.
	--
	local function createBlanketEvent()
		return d2e(sync:addBlanketDelay())
	end

	-----
	-- Discards a waiting event.
	--
	local function discardEvent(event)
		local delay = e2d[event]
		if delay.status ~= "wait" then
			log("Error", "Ignored try to cancel a non-waiting event of type ",
				event.etype)
			return
		end
		sync:removeDelay(delay)
	end

	-----
	-- Gets the next not blocked event from queue.
	--
	local function getEvent()
		return d2e(sync:getNextDelay(lysncd.now()))
	end
	
	-----
	-- Gets all events that are not blocked by active events.
	--
	local function getEvents()
		local dlist = sync:getDelays()
		return dl2el(dlist)
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
	-- Returns the delay from a event.
	--    not to be called from user script.
	local function getDelay(event)
		return e2d[event]
	end
	
	-----
	-- Returns the delay list from a event list.
	--    not to be called from user script.
	local function getDelayList(elist)
		return el2dl[elist]
	end
	
	-----
	-- Return the currentsync 
	--    not to be called from user script.
	local function getSync()
		return sync
	end

	-----
	-- public interface.
	-- this one is split, one for user one for runner.
	return {
			createBlanketEvent = createBlanketEvent,
			discardEvent = discardEvent,
			getEvent  = getEvent, 
			getEvents = getEvents, 
			getConfig = getConfig, 
		}, {
			d2e = d2e,
			dl2el = dl2el,
			getDelay = getDelay,
			getDelayList = getDelayList, 
			getSync = getSync,
			setSync = setSync, 
		}
end)()


-----
-- A set of exclude patterns
--
local Excludes = (function()
	
	-----
	-- Turns a rsync like file pattern to a lua pattern.
	-- 
	-- 
	local function toLuaPattern(p)
		local o = p
		p = string.gsub(p, "%%", "%%")
		p = string.gsub(p, "%^", "%^")
		p = string.gsub(p, "%$", "%$")
		p = string.gsub(p, "%(", "%(")
		p = string.gsub(p, "%)", "%)")
		p = string.gsub(p, "%.", "%.")
		p = string.gsub(p, "%[", "%[")
		p = string.gsub(p, "%]", "%]")
		p = string.gsub(p, "%+", "%+")
		p = string.gsub(p, "%-", "%-")
		p = string.gsub(p, "%?", "[^/]")
		p = string.gsub(p, "%*", "[^/]*")
		-- this was a ** before v
		p = string.gsub(p, "%[%^/%]%*%[%^/%]%*", ".*") 
		p = string.gsub(p, "^/", "^") 
		p = string.gsub(p, "/$", "/$") 
		log("Exclude", "toLuaPattern '",o,"' = '",p,'"')
		return p
	end

	-----
	-- Adds a pattern to exclude.
	--
	local function add(self, pattern)
		if self.list[pattern] then
			-- already in the list
			return
		end
		local lp = toLuaPattern(pattern)
		self.list[pattern] = lp
	end

	-----
	-- Adds a list of patterns to exclude.
	--
	local function addList(self, plist)
		for _, v in plist do
			add(self, v)
		end
	end

	-----
	-- loads excludes from a file
	--
	local function loadFile(self, file)
		f, err = io.open(file)
		if not f then
			log("Error", "Cannot open exclude file '",file,"': ", err)
			terminate(-1) -- ERRNO
		end
	    for line in f:lines() do 
			-- lsyncd 2.0 does not support includes
			if not string.match(line, "%s*+") then
				local p = string.match(line, "%s*-?%s*(.*)")
				if p then
					add(self, p)
				end
			end
		end
		f:close()
	end

	-----
	-- Tests if 'file' is excluded.
	--
	local function test(self, file)
		for _, p in pairs(self.list) do
			if (string.match(file, p)) then
				return true
			end
		end
		return false
	end

	-----
	-- Cretes a new exclude set
	--
	local function new() 
		return { 
			list = {},

			-- functions
			add = add,
			adList = addList,
			loadFile = loadFile,
			test = test,
		}
	end

	-----
	-- Public interface
	return { new = new }
end)()

-----
-- Holds information about one observed directory inclusively subdirs.
--
local Sync = (function()

	-----
	-- Syncs that have no name specified by the user script 
	-- get an incremental default name 'Sync[X]'
	--
	local nextDefaultName = 1

	-----
	-- Removes a delay.
	--
	local function removeDelay(self, delay) 
		local found
		for i, d in ipairs(self.delays) do
			if d == delay then
				found = true
				table.remove(self.delays, i)
				break
			end
		end
		
		if not found then
			error("Did not find a delay to be removed!")
		end

		-- free all delays blocked by this one. 
		if delay.blocks then
			for i, vd in pairs(delay.blocks) do
				vd.status = "wait"
			end
		end
	end

	-----
	-- Collects a child process 
	--
	local function collect(self, pid, exitcode)
		local delay = self.processes[pid]
		if not delay then
			-- not a child of this sync.
			return
		end

		if delay.status then
			-- collected an event
			if delay.status ~= "active" then
				error("internal fail, collecting a non-active process")
			end
			InletControl.setSync(self)
			local rc = self.config.collect(InletControl.d2e(delay), exitcode)
			-- TODO honor return codes of the collect?

			removeDelay(self, delay)
			log("Delay","Finish of ",delay.etype," on ",
				self.source,delay.path," = ",exitcode)
		else
			log("Delay", "collected a list")
			InletControl.setSync(self)
			local rc = self.config.collect(InletControl.dl2el(delay), exitcode)
			-- TODO honor return codes of collect?
			for k, d in pairs(delay) do
				if type(k) == "number" then
					removeDelay(self, d)
				end
			end
			log("Delay","Finished list = ",exitcode)
		end
		self.processes[pid] = nil
	end

	-----
	-- Stacks a newDelay on the oldDelay, 
	-- the oldDelay blocks the new Delay.
	--
	-- A delay can block 'n' other delays, 
	-- but is blocked at most by one, the latest delay.
	-- 
	local function stack(oldDelay, newDelay)
		newDelay.status = "block"
		if not oldDelay.blocks then
			oldDelay.blocks = {}
		end
		table.insert(oldDelay.blocks, newDelay)
	end

	-----
	-- Puts an action on the delay stack.
	--
	local function delay(self, etype, time, path, path2)
		log("Function", "delay(", self.config.name,", ",etype,", ",path,")")
		if not path2 then
			-- test for exclusion
			if self.excludes:test(path) then
				log("Exclude", "excluded ",etype," on '",path,"'")
				return
			end
		else
			log("Function", "+ ",path2)
			local ex1 = self.excludes:test(path)
			local ex2 = self.excludes:test(path2)
			if ex1 and ex2 then
				log("Exclude", "excluded '",etype," on '",path,
					"' -> '",path2,"'")
				return
			elseif not ex1 and ex2 then
				-- splits the move if only partly excluded
				log("Exclude", "excluded destination transformed ",etype,
					" to Delete ",path)
				delay(self, "Delete", time, path, nil)
				return
			elseif ex1 and not ex2 then
				-- splits the move if only partly excluded
				log("Exclude", "excluded origin transformed ",etype,
					" to Create.",path2)
				delay(self, "Create", time, path2, nil)
				return
			end
		end

		if etype == "Move" and not self.config.onMove then
			-- if there is no move action defined, 
			-- split a move as delete/create
			log("Delay", "splitting Move into Delete & Create")
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
		if nd.etype == "Blanket" then
			-- always stack blanket events on the last event
			log("Delay", "Stacking blanket event.")
			if #self.delays > 0 then
				stack(self.delays[#self.delays], nd)
			end
			addDelayPath("", nd)
			table.insert(self.delays, nd)
			return
		end

		-----
		-- detects blocks and collapses by working from back until 
		-- front through the fifo
		InletControl.setSync(self)
		local ne, ne2 = InletControl.d2e(nd)
		local il = #self.delays -- last delay
		while il > 0 do
			-- get 'old' delay
			local od = self.delays[il]
			-- tries to collapse identical paths
			local oe, oe2 = InletControl.d2e(od)

			if oe.etype == "Blanket" then
				-- everything is blocked by a blanket event.
				log("Delay", "Stacking ",nd.etype," upon blanket event.")
				stack(od, nd)
				table.insert(self.delays, nd)
				return
			end

			-- this mini loop repeats the collapse a second 
			-- time for move events
			local oel = oe
			local nel = ne

			while oel and nel do
				local c = self.config.collapse(oel, nel, self.config)
				if c == 0 then
					-- events nullificate each ether
					od.etype = "None"  -- TODO better remove?
					return
				elseif c == 1 then
					log("Delay",nd.etype," is absored by event ",
						od.etype," on ",path)
					return
				elseif c == 2 then
					if od.etype ~= "Move" then
						log("Delay",nd.etype," replaces event ",
							od.etype," on ",path)
						od.etype = nd.etype
						if od.path ~= nd.path then
							error("Cannot replace events with different paths")
						end
					else
						log("Delay",nd.etype," turns a Move into delete of ",
							od.path)
						od.etype = "Delete"
						od.path2 = nil
						table.insert(self.delays, nd)
					end
					return
				elseif c == 3 then
					log("Delay", "Stacking ",nd.etype," upon ",
						od.etype," on ",path)
					stack(od, nd)
					table.insert(self.delays, nd)
					return
				end
				
				-- loops over all oe, oe2, ne, ne2 combos.
				if oel == oe and oe2 then
					-- do another time for oe2 if present
					oel = oe2
				elseif nel == ne then
					-- do another time for ne2 if present
					-- start with first oe
					nel = ne2
					oel = oe
				else 
					oel = false
				end
			end
			il = il - 1
		end
		log("Delay", "Registering ",nd.etype," on ",path)
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

		-- finds the nearest delay waiting to be spawned
		for _, d in ipairs(self.delays) do
			if d.status == "wait" then
				return d.alarm
			end
		end

		-- nothing to spawn.
		return nil
	end
		
	
	-----
	-- Gets all delays that are not blocked by active delays.
	--
	local function getDelays(self)
		local dlist = {}
		local blocks = {}

		----
		-- inheritly transfers all blocks from delay
		--
		local function getBlocks(delay) 
			blocks[delay] = true
			for i, d in ipairs(delay.blocks) do
				getBlocks(d)
			end
		end

		for i, d in ipairs(self.delays) do
			if d.status == "active" then
				getBlocks(d)
			elseif not blocks[d] then
				dlist[i] = d
			end
		end
		return dlist
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
			if #self.delays < self.config.maxDelays then
				-- time constrains only are only a concern if not maxed 
				-- the delay FIFO already.
				if d.alarm ~= true and lsyncd.clockbefore(now, d.alarm) then
					-- reached point in stack where delays are in future
					return
				end
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
	
	-----
	-- Gets the next event to be processed.
	--
	local function getNextDelay(self, now)
		for i, d in ipairs(self.delays) do
			if #self.delays < self.config.maxDelays then
				-- time constrains only are only a concern if not maxed 
				-- the delay FIFO already.
				if d.alarm ~= true and lsyncd.clockbefore(now, d.alarm) then
					-- reached point in stack where delays are in future
					return nil
				end
			end
			if d.status == "wait" then
				-- found a waiting delay
				return d
			end
		end
	end


	------
	-- adds and returns a blanket delay thats blocks all 
	-- (used in startup)
	--
	local function addBlanketDelay(self)
		local newd = Delay.new("Blanket", true, "")
		table.insert(self.delays, newd)
		return newd 
	end
	
	-----
	-- Writes a status report about delays in this sync.
	--
	local function statusReport(self, f)
		local spaces = "                    "
		f:write(self.config.name," source=",self.source,"\n")
		f:write("There are ",#self.delays, " delays\n")
		for i, vd in ipairs(self.delays) do
			local st = vd.status
			f:write(st, string.sub(spaces, 1, 7 - #st))
			f:write(vd.etype," ")
			-- TODO spaces
			f:write(vd.path)
			if (vd.path2) then
				f:write(" -> ",vd.path2)
			end
			f:write("\n")
		end
		f:write("Excluding:\n")
		local nothing = true
		for t, p in pairs(self.excludes.list) do
			nothing = false
			f:write(t,"\n")
		end
		if nothing then
			f:write("  nothing.\n")
		end

		f:write("\n\n")
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
			excludes = Excludes.new(),

			-- functions
			collect         = collect,
			delay           = delay,
			addBlanketDelay = addBlanketDelay,
			getAlarm        = getAlarm,
			getDelays       = getDelays,
			getNextDelay    = getNextDelay,
			invokeActions   = invokeActions,
			removeDelay     = removeDelay,
			statusReport    = statusReport,
		}
		-- provides a default name if needed
		if not config.name then
			config.name = "Sync" .. nextDefaultName
		end
		-- increments default nevertheless to cause less confusion
		-- so name will be the n-th call to sync{}
		nextDefaultName = nextDefaultName + 1

		-- loads exclusions
		if config.exclude then
			s.excludes:addList(config.exclude)
		end
		if config.excludeFrom then
			s.excludes:loadFile(config.excludeFrom)
		end

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
		
		-- at very first let the userscript 'prepare' function 
		-- fill out more values.
		if type(config.prepare) == "function" then
			-- give explicitly a writeable copy of config.
			config.prepare(config)
		end 

		if not config["source"] then
			local info = debug.getinfo(3, "Sl")
			log("Error", info.short_src, ":", info.currentline,
				": source missing from sync.")
			terminate(-1) -- ERRNO
		end
		
		-- absolute path of source
		local realsrc = lsyncd.realdir(config.source)
		if not realsrc then
			log("Error", "Cannot access source directory: ",config.source)
			terminate(-1) -- ERRNO
		end
		config._source = config.source
		config.source = realsrc

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
		local defaultValues = {
			'action',  
			'collapse', 
			'collapseTable', 
			'collect', 
			'init',     
			'maxDelays', 
			'maxProcesses', 
		}
		for _, dn in pairs(defaultValues) do
			if config[dn] == nil then
				config[dn] = settings[dn] or default[dn]
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
	local function removeSync(sync, path)
	    local sp = syncpaths[sync]
		if not sp then
			error("internal fail, removeSync, nonexisting sync: ")
		end
		local wd = sp[path]
		if not wd then
			error("internal fail, removeSync, nonexisting wd.")
		end
		local ilist = wdlist[wd]
		if not ilist then
			error("internal fail, removeSync, nonexisting ilist.")
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
			error("internal fail, removeSync, nonexisiting i.")
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
	-- @param wd        watch descriptor (matches lsyncd.inotifyadd())
	-- @param isdir     true if filename is a directory
	-- @param time      time of event
	-- @param filename  string filename without path
	-- @param filename2 
	--
	local function event(etype, wd, isdir, time, filename, filename2)
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
					removeSync(inotify.sync, path)
					add(inotify.root, path2, true, inotify.sync)
				end
			end
		end
	end

	-----
	-- Writes a status report about inotifies to a filedescriptor
	--
	local function statusReport(f)
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
		statusReport = statusReport 
	}
end)()

------
-- Writes functions for the user for layer 3 configuration.
--
local functionWriter = (function()

	-----
	-- all variables for layer 3
	transVars = {
		{ "%^pathname",          "event.pathname"        , 1, },
		{ "%^path",              "event.path"            , 1, },
		{ "%^sourcePathname",    "event.sourcePathname"  , 1, },
		{ "%^sourcePath",        "event.sourcePath"      , 1, },
		{ "%^source",            "event.source"          , 1, },
		{ "%^targetPathname",    "event.targetPathname"  , 1, },
		{ "%^targetPath",        "event.targetPath"      , 1, },
		{ "%^target",            "event.target"          , 1, },
		{ "%^o%.pathname",       "event.pathname"        , 1, },
		{ "%^o%.path",           "event.path"            , 1, },
		{ "%^o%.sourcePathname", "event.sourcePathname"  , 1, },
		{ "%^o%.sourcePath",     "event.sourcePath"      , 1, },
		{ "%^o%.targetPathname", "event.targetPathname"  , 1, },
		{ "%^o%.targetPath",     "event.targetPath"      , 1, },
		{ "%^d%.pathname",       "event2.pathname"       , 2, },
		{ "%^d%.path",           "event2.path"           , 2, },
		{ "%^d%.sourcePathname", "event2.sourcePathname" , 2, },
		{ "%^d%.sourcePath",     "event2.sourcePath"     , 2, },
		{ "%^d%.targetPathname", "event2.targetPathname" , 2, },
		{ "%^d%.targetPath",     "event2.targetPath"     , 2, },
	}

	-----
	-- Splits a user string into its arguments
	-- 
	-- @param a string where parameters are seperated by spaces.
	--
	-- @return a table of arguments
	--
	local function splitStr(str)
		local args = {}
		while str ~= "" do
			-- break where argument stops
			local bp = #str
			-- in a quote
			local inQuote = false
			-- tests characters to be space and not within quotes
			for i=1,#str do
				local c = string.sub(str, i, i)
				if c == '"' then
					inQuote = not inQuote
				elseif c == ' ' and not inQuote then
					bp = i - 1
					break
				end
			end
			local arg = string.sub(str, 1, bp)
			arg = string.gsub(arg, '"', '\\"')
			table.insert(args, arg)
			str = string.sub(str, bp + 1, -1)
			str = string.match(str, "^%s*(.-)%s*$")
		end
		return args
	end

	-----
	-- Translates a call to a binary to a lua function.
	--
	-- TODO this has a little too much coding blocks.
	--
	local function translateBinary(str)
		-- splits the string
		local args = splitStr(str)
	
		-- true if there is a second event
		local haveEvent2 = false
	
		for ia, iv in ipairs(args) do
			-- a list of arguments this arg is split to
			local a = {{true, iv}}
			-- goes through all translates
			for _, v in ipairs(transVars) do
				ai = 1 
				while ai <= #a do
					if a[ai][1] then
						local pre, post = 
							string.match(a[ai][2], "(.*)"..v[1].."(.*)")
						if pre then
							if pre ~= "" then
								table.insert(a, ai, {true, pre})
								ai = ai + 1
							end
							a[ai] = {false, v[2]}
							if post ~= "" then
								table.insert(a, ai + 1, {true, post})
							end
						end
					end
					ai = ai + 1
				end
			end

			local as = ""
			local first = true
			for _, v in ipairs(a) do
				if not first then
					as = as .. " .. "
				end
				if v[1] then
					as = as .. '"' .. v[2] .. '"'
				else 
					as = as .. v[2]
				end
				first = false
			end
			args[ia] = as
		end

		local ft
		if not haveEvent2 then
			ft = "function(event)\n"
		else
			ft = "function(event, event2)\n"
		end
		ft = ft .. '    log("Normal", "Event " .. event.etype ..\n'
		ft = ft .. "        [[ spawns action '" .. str .. '\']])\n'
		ft = ft .. "    spawn(event"
		for _, v in ipairs(args) do
			ft = ft .. ",\n         " .. v 
		end
		ft = ft .. ")\nend"	
		return ft
	end

	-----
	-- Translates a call using a shell to a lua function
	--
	local function translateShell(str)
		local argn = 1
		local args = {}
		local cmd = str
		local lc = str
		-- true if there is a second event
		local haveEvent2 = false

		for _, v in ipairs(transVars) do
			local occur = false
			cmd = string.gsub(cmd, v[1], 
				function() 
					occur = true
					return '"$'..argn..'"' 
				end)
			lc = string.gsub(lc, v[1], ']]..'..v[2]..'..[[')
			if occur then
				argn = argn + 1
				table.insert(args, v[2])
				if v[3] > 1 then
					haveEvent2 = true
				end
			end
		end
		local ft
		if not haveEvent2 then
			ft = "function(event)\n"
		else
			ft = "function(event, event2)\n"
		end
		ft = ft .. '    log("Normal", "Event " .. event.etype ..\n'
		ft = ft .. "        [[ spawns shell '" .. lc .. '\']])\n'
		ft = ft .. "    spawnShell(event, [[" .. cmd .. "]]"
		for _, v in ipairs(args) do
			ft = ft .. ",\n         " .. v 
		end
		ft = ft .. ")\nend"
		return ft
	end

	-----
	-- writes a lua function for a layer 3 user script.
	local function translate(str)
		-- trim spaces 
		str = string.match(str, "^%s*(.-)%s*$")

		local ft
		if string.byte(str, 1, 1) == 47 then
			 ft = translateBinary(str)
		else
			 ft = translateShell(str)
		end
		log("FWrite","translated [[",str,"]] to \n",ft)
		return ft
	end

	-----
	-- public interface
	--
	return {translate = translate}
end)()


----
-- Writes a status report file at most every [statusintervall] seconds.
--
--
local StatusFile = (function() 
	-----
	-- Timestamp when the status file has been written.
	local lastWritten = false

	-----
	-- Timestamp when a status file should be written
	local alarm = false

	-----
	-- Returns when the status file should be written
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
		local f, err = io.open(settings.statusFile, "w")
		if not f then
			log("Error", "Cannot open status file '"..settings.statusFile..
				"' :"..err)
			return
		end
		f:write("Lsyncd status report at ", os.date(), "\n\n")
		for i, s in Syncs.iwalk() do
			s:statusReport(f)
			f:write("\n")
		end
		
		Inotifies.statusReport(f)
		f:close()
	end

	-- public interface
	return {write = write, getAlarm = getAlarm}
end)()

--============================================================================
-- lsyncd runner plugs. These functions will be called from core. 
--============================================================================

-----
-- Current status of lsyncd.
--
-- "init"  ... on (re)init
-- "run"   ... normal operation
-- "fade"  ... waits for remaining processes
--
local lsyncdStatus = "init"

----
-- the cores interface to the runner
local runner = {}

-----
-- Called from core whenever lua code failed.
-- Logs a backtrace
--
function runner.callError(message)
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
function runner.collectProcess(pid, exitcode) 
	for _, s in Syncs.iwalk() do
		if s:collect(pid, exitcode) then
			return
		end
	end
end

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
function runner.cycle(now)
	-- goes through all syncs and spawns more actions
	-- if possible
	for _, s in Syncs.iwalk() do
		s:invokeActions(now)
	end

	if settings.statusFile then
		StatusFile.write(now)
	end
end

-----
-- Called by core before anything is "-help" or "--help" is in
-- the arguments.
--
function runner.help()
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
function runner.configure(args)
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
			else
				log("Error","unknown option command line option ", args[i])
				os.exit(-1) -- ERRNO
			end
		end
		i = i + 1
	end

	if #nonopts == 0 then
		runner.help(args[0])
	elseif #nonopts == 1 then
		return nonopts[1]
	else 
		log("Error", "There can only be one config file in command line.")
		os.exit(-1) -- ERRNO
	end
end


----
-- Called from core on init or restart after user configuration.
-- 
function runner.initialize()
	-- creates settings if user didnt
	settings = settings or {}

	-- From this point on, no globals may be created anymore
	lockGlobals()

	-----
	-- transfers some defaults to settings 
	-- TODO: loop
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
	lsyncdStatus = "run";
	lsyncd.configure("running");
	
	local ufuncs = {
		"onAttrib", "onCreate", "onDelete",
		"onModify", "onMove",   "onStartup"
	}
		
	-- translates layer 3 scripts
	for _, s in Syncs.iwalk() do
		-- checks if any user functions is a layer 3 string.
		local config = s.config
		for _, fn in ipairs(ufuncs) do
			if type(config[fn]) == 'string' then
				local ft = functionWriter.translate(config[fn])
				config[fn] = assert(loadstring("return " .. ft))()
			end
		end
	end

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
function runner.getAlarm()
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

	log("Debug", "getAlarm returns: ",alarm)
	return alarm
end


-----
-- Called when an inotify event arrived.
-- Simply forwards it directly to the object.
runner.inotifyEvent = Inotifies.event

-----
-- Collector for every child process that finished in startup phase
--
-- Parameters are pid and exitcode of child process
--
-- Can return either a new pid if one other child process 
-- has been spawned as replacement (e.g. retry) or 0 if
-- finished/ok.
--
function runner.collector(pid, exitcode)
	if exitcode ~= 0 then
		log("Error", "Startup process", pid, " failed")
		terminate(-1) -- ERRNO
	end
	return 0
end

----
-- Called by core when an overflow happened.
--
function runner.overflow()
	log("Error", "--- OVERFLOW on inotify event queue ---")
	terminate(-1) -- TODO reset instead.
end

--============================================================================
-- lsyncd user interface
--============================================================================

-----
-- Main utility to create new observations.
--
function sync(opts)
	if lsyncdStatus ~= "init" then
		error("Sync can only be created on initialization.", 2)
	end
	Syncs.add(opts)
end


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
function spawn(agent, binary, ...)
	if agent == nil or type(agent) ~= "table" then
		error("spawning with an invalid agent", 2)
	end
	local pid = lsyncd.exec(binary, ...)
	if pid and pid > 0 then
		local sync = InletControl.getSync()
		local delay = InletControl.getDelay(agent)
		if delay then
			delay.status = "active"
			sync.processes[pid] = delay
		else 
			local dlist = InletControl.getDelayList(agent)
			if not dlist then
				error("spawning with an unknown agent", 2)
			end
			for k, d in pairs(dlist) do
				if type(k) == "number" then
					d.status = "active"
				end
			end
			sync.processes[pid] = dlist
		end
	end
end

-----
-- Spawns a child process using bash.
--
function spawnShell(agent, command, ...)
	return spawn(agent, "/bin/sh", "-c", command, "/bin/sh", ...)
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
	-- Spawns rsync for a list of events
	--
	action = function(inlet) 
		local elist = inlet.getEvents()
		local config = inlet.getConfig()
		local spaths = elist.getSourcePaths()
		log("Normal", "rsyncing list\n", spaths)
		spawn(elist, "/usr/bin/rsync", 
			"<", spaths, 
			"--delete",
			config.rsyncOps.."d",
			"--include-from=-",
			"--exclude=\"*\"",
			config.source, config.target)
	end,

	-----
	-- Spawns the recursive startup sync
	-- 
	init = function(inlet)
		local config = inlet.getConfig()
		local event = inlet.createBlanketEvent()
		if string.sub(config.target, -1) ~= "/" then
			config.target = config.target .. "/"
		end
		log("Normal", "recursive startup rsync: ", config.source,
			" -> ", config.target)
		spawn(event, "/usr/bin/rsync", 
			"--delete",
			config.rsyncOps.."r", 
			config.source, 
			config.target)
	end,

	-----
	-- Calls rsync with this options
	--
	rsyncOps = "-lts",
	
	-----
	-- Default delay 3 seconds
	--
	delay = 3,
}

-----
-- The default table for the user to access 
--   TODO make readonly
-- 
default = {

	-----
	-- Default action calls user scripts on**** functions.
	--
	action = function(inlet)
		-- in case of moves getEvent returns the origin and dest of the move
		local event, event2 = inlet.getEvent()
		local config = inlet.getConfig()
		local func = config["on".. event.etype]
		if func then
			func(event, event2)
		end
		-- if function didnt change the wait status its not interested
		-- in this event -> drop it.
		if event.status == "wait" then
			inlet.discardEvent(event)
		end
	end,

	-----
	-- Called to see if two events can be collapsed.
	--
	-- Default function uses the collapseTable.
	--
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
			local e1 = event1.etype .. event1.move
			local e2 = event2.etype .. event2.move
			return config.collapseTable[e1][e2]
		end
	
		-----
		-- Block events if one is a parent directory of another
		--
		if event1.isdir and string.starts(event2.path, event1.path) then
			return 3
		end
		if event2.isdir and string.starts(event1.path, event2.path) then
			return 3
		end

		return -1
	end,
	
	-----
	-- Used by default collapse function.
	-- Specifies how two event should be collapsed when here 
	-- horizontal event meets upon a vertical event.
	-- values:
	-- 0 ... nullification of both events.
	-- 1 ... absorbtion of horizontal event.
	-- 2 ... replace of vertical event.
	-- 3 ... stack both events, vertical blocking horizonal.
	-- 9 ... combines two move events.
	--
	collapseTable = {
		Attrib = {Attrib=1, Modify=2, Create=2, Delete=2, MoveFr=3, MoveTo= 2},
		Modify = {Attrib=1, Modify=1, Create=2, Delete=2, MoveFr=3, MoveTo= 2},
		Create = {Attrib=1, Modify=1, Create=1, Delete=0, MoveFr=3, MoveTo= 2},
		Delete = {Attrib=1, Modify=1, Create=3, Delete=1, MoveFr=3, MoveTo= 2},
		MoveFr = {Attrib=3, Modify=3, Create=3, Delete=3, MoveFr=3, MoveTo= 3},
--		MoveTo = {Attrib=3, Modify=3, Create=2, Delete=2, MoveFr=9, MoveTo= 2}, TODO 9
		MoveTo = {Attrib=3, Modify=3, Create=2, Delete=2, MoveFr=3, MoveTo= 2},
	},

	-----
	-- Called when collecting a finished child process
	--
	collect = function(agent, exitcode)
		if agent.isList then
			log("Normal", "Finished a list = ",exitcode)
		else
			if agent.etype == "Blanket" then
				if exitcode == 0 then
					log("Normal", "Startup of '",agent.source,"' finished.")
				else
					log("Error", "Failure on startup of '",agent.source,"'.")
					terminate(-1) -- ERRNO
				end
				return
			end
			log("Normal", "Finished ",agent.etype,
				" on ",agent.sourcePath," = ",exitcode)
		end
	end,

	-----
	-- called on (re)initalizing of lsyncd.
	--
	init = function(inlet)
		local config = inlet.getConfig()
		-- user functions

		-- calls a startup if given by user script.
		if type(config.onStartup) == "function" then
			local event = inlet.createBlanketEvent()
			local startup = config.onStartup(event)
			if event.status == "wait" then
				-- user script did not spawn anything
				-- thus the blanket event is deleted again.
				inlet.discardEvent(event)
			end
			-- TODO honor some return codes of startup like "warmstart".
		end
	end,

	-----
	-- The maximum number of processes lsyncd will spawn simultanously for
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
	-- a default rsync configuration for easy usage.
	--
	rsync = defaultRsync,

	-----
	-- Minimum seconds between two writes of a status file.
	--
	statusIntervall = 10,
}

-----
-- Returns the core the runners function interface.
--
return runner
