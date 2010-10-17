------------------------------------------------------------------------------
-- lsyncd library functions implemented in C
------------------------------------------------------------------------------
----
-- real_dir(dir) 
--
-- Converts a relative directory path to an absolute.
--
-- @param dir  a relative path to directory
-- @return     absolute path of directory
--
----
--
-- sub_dirs(dir)
--
-- Reads the directories sub directories.
--
-- @param dir  absolute path to directory.
-- @return     a table of directory names.
--

------------------------------------------------------------------------------
-- lsyncd library functions implemented in LUA
------------------------------------------------------------------------------

----
-- Table of all directories to watch.
local origin = {}

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
-- @param ...
local function attend_dir(origin, path, target)
	print("attending dir", origin, "+", path, "->", target.dirname);
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
	for i, o in ipairs(origin) do
		print("Handling ", o.source, "->" , o.target)
		local target = { dirname = o.target }
		table.insert(targets, target)
		attend_dir(lsyncd.real_dir(o.source), "", target)
	end
end


------------------------------------------------------------------------------
-- lsyncd user interface
------------------------------------------------------------------------------

----
-- Add one directory to be watched.
function add(source, target)
	local o = { source = source, target = target }
	table.insert(origin, o)
	return o
end


