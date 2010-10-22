----
-- User configuration file for lsyncd.
--
-- TODO documentation-
--
settings = {
	logfile = "/tmp/lsyncd",
	nodaemon,
}

rsync = {
	default = function(source, path, target)
		return exec("/usr/bin/rsync", "--delete", "-ltds", source .. "/" .. path, target .. "/" .. path)
	end
}

directory("s", "d", rsync)

----
-- Called for every source .. target pair on startup
-- Returns the pid of a spawned process
-- Return 0 if you dont exec something.
function startup_action(source, target)
	log(NORMAL, "startup recursive rsync: " .. source .. " -> " .. target)
	return exec("/usr/bin/rsync", "-ltrs", source, target)
end

