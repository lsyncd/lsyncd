----
-- User configuration file for lsyncd.
--
-- TODO documentation-
--
settings = {
--	logfile = "/tmp/lsyncd",
--	nodaemon = true,
	statusfile = "/tmp/lsyncd.stat",
	statusintervall = 1,
}

----
-- for testing purposes. uses bash command to hold local dirs in sync.
--
prefix = "sleep 1 && "
slowbash = {
	delay = 5,

	init = function(inlet)
		local c = inlet.getConfig()
		log("Normal", "cp -r from ", c.source, " -> ", c.target)

		-- collect gets called when spawned process finished
		local function collect(exitcode)
			if exitcode == 0 then
				log("Normal", "Startup of '",c.source,"' finished.")
			else
				log("Error", "Failure on startup of '",c.source,"'.")
				terminate(-1) -- ERRNO
			end
		end

		spawnShell(inlet.createBlanketEvent(), collect,
			[[if [ "$(ls -A $1)" ]; then cp -r "$1"* "$2"; fi]], 
			c.source, c.target)
	end,

	onCreate = function(event)
		local s = event.sourcePathname
		local t = event.targetPathname
		log("Normal", "Spawning Create ", s," -> ",t)
		spawnShell(event, "ok", prefix..[[cp -r "$1" "$2"]], s, t)
	end,

	onModify = function(event)
		local s = event.sourcePathname
		local t = event.targetPathname
		log("Normal", "Spawning Modify ",s," -> ",t)
		spawnShell(event, "ok", prefix..[[cp -r "$1" "$2"]], s, t)
	end,

	onDelete = function(event)
		local t = event.targetPathname
		log("Normal", "Spawning Delete of ",t)
		spawnShell(event, "ok", prefix..[[rm -rf "$1"]], t)
	end,

	onMove = function(event, eventd)
		local t = event.targetPathname
		local d = eventd.targetPathname
		log("Normal", "Spawning Move from ",t," to ",d)
		spawnShell(event, "ok", prefix..[[mv "$1" "$2"]], t, d)
	end,
}

sync{slowbash, source="s", target="d/"}

