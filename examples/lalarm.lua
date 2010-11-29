-----
-- User configuration file for lsyncd.
-- 
-- While this example does not do anything it shows
-- how user custom alarms can be now. It will log 
-- "Beep!" every 5 seconds.
--

----
-- Defines a function to be called when the alarm
-- is raised. 
-- @param timestamp ... timestamp this alarm was registered with
-- @param extra     ... a free token to store anything in it.
--                      here used as string.
--
local function myAlarm(timestamp, extra)
	log("Normal", extra)

	-- creates a new alarm in 5 seconds after this one rang
	alarm(timestamp + 5, myAlarm, extra)
end

-- creates the first alarm in 5 seconds from now.
alarm(now() + 5, myAlarm, "Beep!")

-----
-- Just a minimal dummy sync in sake for this example. 
-- Lsyncd needs to feel like it is doing something useful.
-- Any real application needs to watch anything otherwise
-- probably shouldn't use Lsyncd :-)
sync{source="/usr/local/etc/", onModify = function() end }

