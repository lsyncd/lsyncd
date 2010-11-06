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

	onCreate = function(event)
		local s = event.sourcePathname
		local t = event.targetPathname
		log("Normal", "create from ", s, " -> ", t)
		return shell(prefix..[[cp -r "$1" "$2"]], s, t)
	end,

	onModify = function(event)
		local s = event.sourcePathname
		local t = event.targetPathname
		log("Normal", "modify from ", s, " -> ", t)
		return shell(prefix..[[cp -r "$1" "$2"]], s, t)
	end,

	onDelete = function(event)
		local t = event.targetPathname
		log("Normal", "delete ", t)
		return shell(prefix..[[rm -rf "$1"]], t)
	end,

	onMove = function(event)
		local t = event.targetPathname
		local d = event.dest.targetPathname
		log("Normal", "delete ", t)
		return shell(prefix..[[mv "$1" "$2"]], t, d)
	end,
}

sync{slowbash, source="s", target="d/"}

