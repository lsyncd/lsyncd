----
-- User configuration file for lsyncd.
--
-- TODO documentation-
--
settings = {
--	logfile = "/tmp/lsyncd",
--	nodaemon = true,
	statusfile = "/tmp/lsyncd.stat",
}

------
-- for testing purposes. uses bash command to hold local dirs in sync.
--
prefix = "sleep 1 && "
slowbash = {
	delay = 5,

	startup = function(source, target)
		log("Normal", "cp -r from ", source, " -> ", target)
		return shell([[if [ "$(ls -A $1)" ]; then cp -r "$1"* "$2"; fi]], 
			source, target)
	end,

	create = function(event)
		log("Normal", 
			"create from ", event.sourcebasename,
			" -> ", event.targetbasename)
		return shell(prefix..[[cp "$1" "$2"]],
			event.sourcebasename, event.targetbasename)
	end,

	modify = function(event)
		log("Normal", 
			"modify from ", event.sourcename,
			" -> ", event.targetname)
		return shell(prefix..[[cp "$1" "$2"]],
			event.sourcebasename, event.targetbasename)
	end,

	attrib = function(event)
		-- ignore attribs
		return 0
	end,

	delete = function(event)
		log("Normal", "delete "..event.targetbasename)
		return shell(prefix..[[rm "$1"]], event.targetbasename)
	end,

--	move  = function(event)
--	end,
}


sync{slowbash, source="s", target="d/"}

