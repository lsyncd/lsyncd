----
-- User configuration file for lsyncd.
--
-- TODO documentation-
--
settings = {
--	logfile = "/tmp/lsyncd",
	nodaemon,
	statuspipe = "/tmp/lsyncd.stat",
	loglevel = DEBUG,
}

------
-- for testing purposes
--
prefix = "sleep 1 && "
slowbash = {
	delay = 5,

	startup = function(source, target)
		log(NORMAL, "cp -r from "..source.." -> "..target)
		return shell([[if [ "$(ls -A $1)" ]; then cp -r "$1"* "$2"; fi]], source, target)
	end,

	create = function(source, path, name, target)
		local src = source..path..name
		local trg = target..path..name
		log(NORMAL, "create from "..src.." -> "..trg)
		return shell(prefix..[[cp "$1" "$2"]], src, trg)
	end,

	modify = function(source, path, name, target)
		local src = source..path..name
		local trg = target..path..name
		log(NORMAL, "modify from "..src.." -> "..trg)
		return shell(prefix..[[cp "$1" "$2"]], src, trg)
	end,

	attrib = function(source, path, name, target)
		-- ignore attribs
		return 0
	end,

	delete = function(source, path, name, target)
		local trg = target..path..name
		log(NORMAL, "delete "..trg)
		return exec(prefix..[[rm "$1"]], trg)
	end,

--	move  = function(source, path, name, destpath, destname, target)
--		log(NORMAL, "move from " .. destination .. "/" .. path)
--		return exec("/bin/bash", "-c", "sleep " .. slowsec .. " && rm $1 $2", "/bin/bash", 
--		            source .. "/" .. path, target .. "/" .. path)
--		return 0
--	end,
}


sync("s", "d/", slowbash)


