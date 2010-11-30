-----
-- User configuration file for lsyncd.
-- 
-- While this example does not do anything it shows
-- how user custom alarms can be now. It will log 
-- "Beep!" every 5 seconds.
--

lalarm = {
	init = function(inlet)
		-- creates the first alarm in 5 seconds from now.
		inlet.alarm(now() + 5, "Beep")
	end,

	-- called when alarms ring
	alarm = function(inlet, timestamp, extra)
		log("Normal", extra)
		
		spawn(inlet.createBlanketEvent(), "/bin/echo", "hello")
		-- creates a new alarm in 5 seconds after this one rang
		inlet.alarm(timestamp + 5, extra)
	end,

	action = function(inlet)
		-- just discard anything that happes in source dir.
		inlet.discardEvent(inlet.getEvent())
	end
}

-----
-- Lsyncd needs to watch something, altough in this minimal example
-- it isnt used.
sync{source="/usr/local/etc/", lalarm }

