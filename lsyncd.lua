------------------------------------------------------------------------------
-- lsyncd runner implemented in LUA
------------------------------------------------------------------------------

----
-- Table of all directories to watch.
local origins = {}

----
-- all targets
local targets = {}

-----
-- all watches
local watches = {}

----
-- Adds watches for a directory including all subdirectories.
--
-- @param sdir
-- @param target
--
local function attend_dir(origin, path, target)
	print("attending dir", origin, "+", path, "->", target.path);
	-- actual dir = origin + path 
	local op = origin .. path
	-- register watch and receive watch descriptor
	local wd = lsyncd.add_watch(op);
	if wd < 0 then
		-- failed adding the watch
		-- TODO die?
		return
	end

	if watches[wd] ~= nil then
		-- this directory is already watched, add the target
		local watch = watches[wd]
		table.insert(watch.attends, { origin = origin, path = path, target = target })
	else
		-- new watch
		local watch = {wd = wd, attends = { origin = origin, path = path, target = target } }
		watches[wd] = watch
	end

	-- register all subdirectories 
	local subd = lsyncd.sub_dirs(op);
	local i, o
	for i, v in ipairs(subd) do
		attend_dir(origin, path .. v .. "/", target)
	end
end

----
-- Called from core on init or restart after user configuration.
-- 
function lsyncd_initialize()
	print("--- INIT ---")
	local i, o
	for i, o in ipairs(origins) do
		print("Handling ", o.source, "->" , o.targetpath)
		-- resolves source to be an absolute path
		local src = lsyncd.real_dir(o.source)
		if src == nil then
			print("Cannot resovle source path: ", src)
			lsyncd.terminate() -- ERRNO
		end
		o.source = src

		-- appends the target on target lists
		local target = { path = o.targetpath }
		table.insert(targets, target)
		o.target = target

		-- and add the dir watch inclusively all subdirs
		attend_dir(o.source, "", target)
	end
end


------------------------------------------------------------------------------
-- lsyncd user interface
------------------------------------------------------------------------------

----
-- Adds one directory to be watched.
--
function add(source_dir, target_path)
	local o = { source = source_dir, targetpath = target_path }
	table.insert(origins, o)
	return o
end

-----
-- Called by core after initialization.
--
-- Returns a table of integers (pid of children) the core will 
-- wait for before entering normal operation.
--
-- User can override this function by specifing his/her own
-- "startup". (and yet may still call default startup)
--
function default_startup()
	print("--- STARTUP ---")
	local pids = { }
	for i, o in ipairs(origins) do
		print("/usr/bin/rsync", "-ltrs", o.source, o.targetpath)
		pid = lsyncd.exec("/usr/bin/rsync", "-ltrs", o.source, o.targetpath)
		print("started ", pid)
		table.insert(pids, pid)
	end
	return pids
end
startup = default_startup

-----
-- Called by the core for every child process that 
-- finished in startup phase
--
-- Parameters are pid and exitcode of child process
--
-- Can returns either a new pid if another child process 
-- has been spawned as replacement (e.g. retry) or 0 if
-- finished/ok.
--
function default_startup_returned(pid, exitcode)
	print("startup_returned ", pid, exitcode);
	if exitcode ~= 0 then
		print("Startup process", pid, " failed")
		lsyncd.terminate(-1) -- ERRNO
	end
	return 0
end
startup_returned = default_startup_returned

