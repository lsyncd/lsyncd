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
-- Adds watches for a directory including all subdirectories.
--
-- @param src 
-- @param dst
-- @param ...
function attend_dir(src, dst, ...)
	src = real_dir(src);
	print("attending dir", src, "->", dst);

	local sd = sub_dirs(src);
	for k, v in ipairs(sd) do
		print("havesub", v);
	end
end


