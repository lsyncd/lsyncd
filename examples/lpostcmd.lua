-----
-- User configuration file for lsyncd.
--
-- This configuration will execute a command on the remote host
-- after every successfullycompleted rsync operation.
-- for example to restart servlets on the target host or so.

local rsyncpostcmd = {

	-- based on default rsync.
	default.rsync,

    -- for this config it is important to keep maxProcesses at 1, so
    -- the postcmds will only be spawned after the rsync completed
	maxProcesses = 1,

	-- called whenever something is to be done
	action = function(inlet)
		local event = inlet.getEvent()
		-- if the event is a blanket event and not the startup,
		-- its there to spawn the webservice restart at the target.
		if event.etype == "Blanket" then
			-- uses rawget to test if "isRestart" has been set without
			-- triggering an error if not.
			local isRestart = rawget(event, "isRestart")
			if event.isRestart then
				spawn(event, "/usr/bin/ssh", host, postcmd)
        		return
			else
            	-- this is the startup, forwards it to default routine.
            	return default.rsync.action(inlet) 
        	end
			error("this should never be reached")
		end
     	-- for any other event, an blanket event is created that
		-- will stack on the queue and do the postcmd when its finished
		local sync = inlet.createBlanketEvent()
		sync.isRestart = true
		-- the original event if forward to the normal action handler
		return default.rsync.action(inlet)
	end,

	collect = function(agent, exitcode)
		-- for the restart ssh commands 255 is network error -> try again
		local isRestart = rawget(agent, "isRestart")
		if not agent.isList and agent.etype == "Blanket" and isRestart then
			if exitcode == 255 then
				return "again"
			end
			return
		else
			--- everything else, forward to default collection handler
			return default.collect(agent,exitcode)
		end
		error("this should never be reached")
	end

	prepare = function(config)
		if not config.host then
			error("rsyncpostcmd neets 'host' configured", 4)
		end
		if not config.targetdir then
			error("rsyncpostcmd needs 'targetdir' configured", 4)
		end
		if not config.target then
			config.target = config.host .. ":" .. config.targetdir
		end
		return default.rsync.prepare(config)
	end
}


sync {
	rsyncpostcmd, 
	source = "src",
	host = "beetle",
	targetdir = "/path/to/trg",
	postcmd = "/usr/local/bin/restart-servelt.sh",
}

