-----
-- User configuration file for lsyncd.
-- This needs lsyncd >= 2.0.3
--
-- This configuration will execute a command on the remote host
-- after every successfullycompleted rsync operation.
-- for example to restart servlets on the target host or so.

local rsyncpostcmd = {

	-- based on default rsync.
	default.rsync,

	checkgauge = {
		default.rsync.checkgauge,
		host = true,
		targetdir = true,
		target = true,
		postcmd = true,
	},

	-- for this config it is important to keep maxProcesses at 1, so
	-- the postcmds will only be spawned after the rsync completed
	maxProcesses = 1,

	-- called whenever something is to be done
	action = function(inlet)
		local event = inlet.getEvent()
		local config = inlet.getConfig()
		-- if the event is a blanket event and not the startup,
		-- its there to spawn the webservice restart at the target.
		if event.etype == "Blanket" then
			-- uses rawget to test if "isPostcmd" has been set without
			-- triggering an error if not.
			local isPostcmd = rawget(event, "isPostcmd")
			if isPostcmd then
				spawn(event, "/usr/bin/ssh",
					config.host, config.postcmd)
        		return
			else
            	-- this is the startup, forwards it to default routine.
            	return default.rsync.action(inlet)
        	end
			error("this should never be reached")
		end
		-- for any other event, a blanket event is created that
		-- will stack on the queue and do the postcmd when its finished
		local sync = inlet.createBlanketEvent()
		sync.isPostcmd = true
		-- the original event is simply forwarded to the normal action handler
		return default.rsync.action(inlet)
	end,

	-- called when a process exited.
	-- this can be a rsync command, the startup rsync or the postcmd
	collect = function(agent, exitcode)
		-- for the ssh commands 255 is network error -> try again
		local isPostcmd = rawget(agent, "isPostcmd")
		if not agent.isList and agent.etype == "Blanket" and isPostcmd then
			if exitcode == 255 then
				return "again"
			end
			return
		else
			--- everything else, forward to default collection handler
			return default.collect(agent,exitcode)
		end
		error("this should never be reached")
	end,

	-- called before anything else
	-- builds the target from host and targetdir
	prepare = function(config, level, skipTarget)
		if not config.host then
			error("rsyncpostcmd neets 'host' configured", 4)
		end
		if not config.targetdir then
			error("rsyncpostcmd needs 'targetdir' configured", 4)
		end
		if not config.target then
			config.target = config.host .. ":" .. config.targetdir
		end
		return default.rsync.prepare(config, level, skipTarget)
	end
}


sync {
	rsyncpostcmd,
	source = "src",
	host = "beetle",
	targetdir = "/path/to/trg",
	postcmd = "/usr/local/bin/restart-servelt.sh",
}

