---
layout: default
title: "Config Layer 1: Inlets"
short: "Config Layer 1" 
---
Layer 2 allows you to create one process per one event. However, as with default rsync behavior you might want to call one process for several events. This can be done with inlets. When any event becomes ready Lsyncd calls the ```action``` entry with ```inlet``` as parameter. The ```inlet``` can be used to grab ready single events or event lists.

For example this is the action used by default.rsync:

{% highlight lua %}
action = function(inlet)
   local elist = inlet.getEvents()
   local config = inlet.getConfig()
   local paths = elist.getPaths()
   log("Normal", "rsyncing list\n", paths)
   spawn(elist, "/usr/bin/rsync",
       "<", paths,
       "--delete",
       config.rsyncOpts .. "d",
       "--include-from=-",
       "--exclude=*",
       config.source, config.target)
end
{% endhighlight %}

Inlet functions are:

| Function | Description |
|:---------|:------------|
| inlet.getEvent() | Retrieves the next `event` as in Layer 2 configuration. Multiple calls to getEvent() will return the same event unless it has spawn{}ed an action. |
| inlet.getEvents(test) | Returns a list of all events that are ready. `test` is optional for a function that will be called for every event to test if it should be included in the list. It has one parameter the `event` and returns true if an event should be included. If nil every ready event will be included in the list |
| inlet.discardEvent() | Discards an event. The next call to getEvent will thus receive another event, even if no action has been spawned for this event |
| inlet.getConfig() | returns the same as `event.config`. The configuration of the sync{} |
| inlet.addExclude() | adds an exclusion pattern to this sync (see Exclusions) |
| inlet.rmExclude() | removes an exclusion pattern from this sync |
| inlet.createBlanketEvent() | puts an `event` on the top of the Delay FIFO that blocks all events and is blocked by all events. This is used for onStartup.  |

The list returned by getEvents can be handed to spawn{} as _agent_ just as well as singular events.

Lists have following functions 

| Function  | Description |
|:----------|:------------|
| elist.getPaths(delimiter) | returns a string of the paths (as in `event.path` separated by `delimiter`. By default \n is used as delimiter. |
| elist.getSourcePaths(delimiter) | returns a string of the sourcePaths (as in `event.sourcePath` separated by `delimiter`. By default \n is used as delimiter. |

Take care calling getEvents() and its function since depending on the amount of events, they will cause quite some CPU load.

Layer 2 functions is nothing else than following layer 1 action loaded by the default if the user script did not provide one itself.

{% highlight lua %}
-----
-- Default action calls user scripts on**** functions.
--
action = function(inlet)
    -- in case of moves getEvent returns the origin and destination of the move
    local event, event2 = inlet.getEvent()
    local config = inlet.getConfig()
    local func = config["on".. event.etype]
    if func then
        func(event, event2)
    end 
    -- if function didnt change the wait status its not interested
    -- in this event -> drop it.
    if event.status == "wait" then
        inlet.discardEvent(event)
    end 
end,
{% endhighlight %}

Lsyncd will automatically split Move events into Create and Delete events if no "onMove" field is found in the config. When handling moves in layer 1 `action` function, simply set "onMove" to be "true". 

Other than `action` Lsyncd calls `init` for each sync{} on initialization. This is the default init function which is loaded if the user script does not have one. It provides the onStartup() functionality for layer 2 and 3.

{% highlight lua %}
-----
-- called on (re)initalizing of lsyncd.
--
init = function(inlet)
    local config = inlet.getConfig()
    -- calls a startup if provided by user script.
    if type(config.onStartup) == "function" then
        local event = inlet.createBlanketEvent()
        config.onStartup(event)
        if event.status == "wait" then
            -- user script did not spawn anything
            -- thus the blanket event is deleted again.
            inlet.discardEvent(event)
        end 
    end 
end,
{% endhighlight %}

As another example this is the init of `default.rsync`. As specialty it changes the configuration in that it adds a slash to target if not there already.

{% highlight lua %}
-----
-- Spawns the recursive startup sync
-- 
init = function(inlet)
    local config = inlet.getConfig()
    local event = inlet.createBlanketEvent()
    if string.sub(config.target, -1) ~= "/" then
        config.target = config.target .. "/" 
    end 
    log("Normal", "recursive startup rsync: ", config.source,
        " -> ", config.target)
    spawn(event, "/usr/bin/rsync", 
        "--delete",
        config.rsyncOpts .. "r", 
        config.source, 
        config.target)
end,
{% endhighlight %}

When child processes are finished and their zombie processes are collected, Lsyncd calls the function of the `collect` entry. When collect return "again" the status of the agent (an event or an event list) will be set on "wait" again, and will become ready in `delay` seconds (or 1 second if smaller).

The default collect function looks in the exitcodes[] table for an entry of the exit code. Otherwise most of the unfortunately longer code below does nothing but making nice log message.

{% highlight lua %}
-----
-- Called when collecting a finished child process
--
collect = function(agent, exitcode)
	local config = agent.config

	if not agent.isList and agent.etype == "Blanket" then
		if exitcode == 0 then
			log("Normal", "Startup of '",agent.source,"' finished.")
		elseif config.exitcodes and 
		       config.exitcodes[exitcode] == "again" 
		then
			log("Normal", 
				"Retrying startup of '",agent.source,"'.")
			return "again"
		else
			log("Error", "Failure on startup of '",agent.source,"'.")
			terminate(-1) -- ERRNO
		end
		return
	end

	local rc = config.exitcodes and config.exitcodes[exitcode] 
	if rc == "die" then
		return rc
	end

	if agent.isList then
		if rc == "again" then
			log("Normal", "Retrying a list on exitcode = ",exitcode)
		else
			log("Normal", "Finished a list = ",exitcode)
		end
	else
		if rc == "again" then
			log("Normal", "Retrying ",agent.etype,
				" on ",agent.sourcePath," = ",exitcode)
		else
			log("Normal", "Finished ",agent.etype,
				" on ",agent.sourcePath," = ",exitcode)
		end
	end
	return rc
end,
{% endhighlight %}
