----
-- User configuration file for lsyncd.
-- 
-- This example uses local bash commands to keep two local 
-- directory trees in sync.
--
settings = {
	statusFile = "/tmp/lsyncd.stat",
	statusIntervall = 1,
}

----
-- for testing purposes. prefix can be used to slow commands down.
-- prefix = "sleep 5 && "
prefix = ""
----
-- for testing purposes. uses bash command to hold local dirs in sync.
--
bash = {
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

sync{bash, source="src", target="dst/"}

