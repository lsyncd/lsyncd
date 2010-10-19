----
-- User configuration file for lsyncd.
--
-- TODO documentation-
--
settings = {
	logfile = "/tmp/lsyncd",
	nodaemon,
}

add("s", "d")
-- add("s/s1", "t")

----
-- Called for every source .. target pair on startup
-- Returns the pid of a spawned process
-- Return 0 if you dont exec something.
function startup_action(source, target)
	print("startup recursive rsync: " .. source .. " -> " .. target)
	return exec("/usr/bin/rsync", "-ltrs", source, target)
end

