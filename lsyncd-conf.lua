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

----
-- for testing purposes. uses bash command to hold local dirs in sync.
--
prefix = "sleep 1 && "
slowbash = {
	delay = 5,

	onStartup = function(config)
		-- called on startup
		local source = config.source
		local target = config.target
		log("Normal", "cp -r from ", source, " -> ", target)
		return shell([[if [ "$(ls -A $1)" ]; then cp -r "$1"* "$2"; fi]], 
			source, target)
	end,

	onCreate = function(config, event)
		-- concats the source and the target with the file/dirs path and name
		-- basename removes the trailing '/' on dirs.
		local source = config.source .. event.pathbasename
		local target = config.target .. event.pathbasename
		log("Normal", "create from ", source, " -> ", target)
		return shell(prefix..[[cp -r "$1" "$2"]], source, target)
	end,

	onModify = function(config, event)
		-- same game for modifies
		local source = config.source .. event.pathbasename
		local target = config.target .. event.pathbasename
		log("Normal", "modify from ", source, " -> ", target)
		return shell(prefix..[[cp -r "$1" "$2"]], source, target)
	end,

	onDelete = function(config, event)
		-- similar for deletes
		local target = config.target .. event.pathbasename
		log("Normal", "delete ", target)
		return shell(prefix..[[rm -rf "$1"]], target)
	end,
}

sync{slowbash, source="s", target="d/"}

