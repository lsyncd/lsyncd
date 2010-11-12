----
-- User configuration file for lsyncd.
--
-- TODO documentation-
--
settings = {
--	logfile = "/tmp/lsyncd",
--	nodaemon = true,
	statusFile = "/tmp/lsyncd.stat",
	statusIntervall = 1,
}

----
-- for testing purposes. uses bash command to hold local dirs in sync.
--
prefix = "sleep 5 && "
slowbash = {
	delay = 5,

	maxProcesses = 3,

	onStartup = function(event)
		log("Normal", "cp -r from ", event.source, " -> ", event.target)
		spawnShell(event,
			[[if [ "$(ls -A $1)" ]; then cp -r "$1"* "$2"; fi]], 
			event.source, event.target)
	end,

	onCreate = function(event)
		local s = event.sourcePathname
		local t = event.targetPathname
		log("Normal", "Spawning Create ", s," -> ",t)
		spawnShell(event, prefix..[[cp -r "$1" "$2"]], s, t)
	end,

	onModify = function(event)
		local s = event.sourcePathname
		local t = event.targetPathname
		log("Normal", "Spawning Modify ",s," -> ",t)
		spawnShell(event, prefix..[[cp -r "$1" "$2"]], s, t)
	end,

	onDelete = function(event)
		local t = event.targetPathname
		log("Normal", "Spawning Delete of ",t)
		spawnShell(event, prefix..[[rm -rf "$1"]], t)
	end,

	onMove = function(originEvent, destinationEvent)
		local t = originEvent.targetPathname
		local d = destinationEvent.targetPathname
		log("Normal", "Spawning Move from ",t," to ",d)
		spawnShell(originEvent, prefix..[[mv "$1" "$2"]], t, d)
	end,
}

sync{slowbash, source="s", target="d/"}

