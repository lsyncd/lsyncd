----
-- User configuration file for lsyncd.
--
-- TODO documentation-
--
settings = {
--	logfile = "/tmp/lsyncd",
--	nodaemon = true,
	statusfile = "/tmp/lsyncd.stat",
	loglevel = "Debug",
}

------
-- for testing purposes. uses bash command to hold local dirs in sync.
--
prefix = "sleep 1 && "
slowbash = {
	delay = 5,

	startup = function(source, target)
		log("Normal", "cp -r from "..source.." -> "..target)
		return shell([[if [ "$(ls -A $1)" ]; then cp -r "$1"* "$2"; fi]], source, target)
	end,

	create = function(event)
		log("Normal", "create from "..event.spath.." -> "..event.tpath)
		return shell(prefix..[[cp "$1" "$2"]], event.spath, event.tpath)
	end,

	modify = function(event)
		log("Normal", "modify from "..event.spath.." -> "..event.tpath)
		return shell(prefix..[[cp "$1" "$2"]], event.spath, event.tpath)
	end,

	attrib = function(event)
		-- ignore attribs
		return 0
	end,

	delete = function(event)
		log("Normal", "delete "..event.tpath)
		return exec(prefix..[[rm "$1"]], event.tpath)
	end,

--	move  = function(source, path, name, destpath, destname, target)
--		log(NORMAL, "move from " .. destination .. "/" .. path)
--		return exec("/bin/bash", "-c", "sleep " .. slowsec .. " && rm $1 $2", "/bin/bash", 
--		            source .. "/" .. path, target .. "/" .. path)
--		return 0
--	end,
}


sync{source="s", target="d/", config=slowbash}

