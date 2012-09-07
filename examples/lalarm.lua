-----
-- User configuration file for lsyncd.
--
-- While this example does not do anything it shows
-- how user custom alarms can be now. It will log
-- "Beep!" every 5 seconds.
--
settings.nodaemon = true

local function noAction (inlet)
	-- just discard any events that happes in source dir.
	inlet.discardEvent(inlet.getEvent())
end

-----
-- Adds a watch to some not so large directory for this example.
local in1 = sync{source="/usr/local/etc/", action = noAction }

local function myAlarm(timestamp, extra)
	log("Normal", extra.message)
	spawn(extra.inlet.createBlanketEvent(), "/bin/echo", extra.message)
	alarm(timestamp + 5, myAlarm, extra)
end

alarm(now() + 5, myAlarm, {inlet = in1, message = "Beep"})

