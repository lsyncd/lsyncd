----
-- User configuration file for lsyncd.
--
-- TODO documentation-
--
settings = {
--	logfile = "/tmp/lsyncd",
--	nodaemon,
	statusfile = "/tmp/lsyncd.stat",
	loglevel = DEBUG,
}

------
-- for testing purposes. uses bash command to hold local dirs in sync.
--
prefix = "sleep 1 && "
slowbash = {
	delay = 5,

	startup = function(source, target)
		log(NORMAL, "cp -r from "..source.." -> "..target)
		return shell([[if [ "$(ls -A $1)" ]; then cp -r "$1"* "$2"; fi]], source, target)
	end,

	create = function(self, events)
		local event = events:nextevent()
		log(NORMAL, "create from "..event.spath.." -> "..event.tpath)
		return shell(prefix..[[cp "$1" "$2"]], event.spath, event.tpath)
	end,

	modify = function(self, events)
		local event = events:nextevent()
		log(NORMAL, "modify from "..event.spath.." -> "..event.tpath)
		return shell(prefix..[[cp "$1" "$2"]], event.spath, event.tpath)
	end,

	attrib = function(self, events)
		-- ignore attribs
		return 0
	end,

	delete = function(self, events)
		local event = events:nextevent()
		log(NORMAL, "delete "..event.tpath)
		return exec(prefix..[[rm "$1"]], event.tpath)
	end,

--	move  = function(source, path, name, destpath, destname, target)
--		log(NORMAL, "move from " .. destination .. "/" .. path)
--		return exec("/bin/bash", "-c", "sleep " .. slowsec .. " && rm $1 $2", "/bin/bash", 
--		            source .. "/" .. path, target .. "/" .. path)
--		return 0
--	end,
}


sync("s", "d/", slowbash)


