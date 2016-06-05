---
layout: default
title: "Config Layer 2: Advanced onAction"
short: "Config Layer 2" 
---
While Layer 4 and 3 feel like normal configuration files, Layer 2 and 1 enter the realm of coding. It is thus supposed you have some coding knowledge when using Layer 2 or 1.

Instead of designating actions as strings as in Layer 3 Lua functions can used to do some small scripts right within Lsyncd.

This example will convert any file with the suffix ".ps" created in a directory into a PDF.

```Lua
autopdf = {
    onCreate = function(event)
        log("Normal", "got an onCreate Event")
        if string.ends(event.pathname, ".ps") then
            spawn(event, "/usr/bin/ps2pdf", event.sourcePath)
        end
    end
}
```

The function can take any valid Lua code. 

Lsyncd provides you a set of functions to be used in user scripts.

log(Category, ...)
------------------
Logs a message into file/stdout/syslog. The first parameter is the logging category all others are strings to be logged. A logging category must start with a capital letter. "Normal" and "Error" are standard categories for log messages. All others are categories for debugging.

spawn(Event, Binary, ...)
--------------------------
Spawns a new process associated with the event (or event list, see below) as first parameter. The second parameter specifies a binary to call. All others are arguments for the binary. 

If the third parameter is "<", then along with fourth parameter they will not be passed as arguments to the binary. The fourth parameter is a string that will piped through stdin to the binary.

Do not use Lua's ```os.execute``` as opposed to Lsyncd's ```spawn()``` it will block and thus block the whole Lsyncd daemon until the command is completed. Lsyncd's ```spawn``` on the other hand returns immediately while the child process runs.

spawnShell(Event, Command, ... )
--------------------------------
The same as spawn(), only it will invoke a shell. Any parameters are referred as $1, $2, $3 and so on in the command. 

By the way, this is the simple implementation of spawnShell:

```Lua
function spawnShell(agent, command, ...)
    return spawn(agent, "/bin/sh", "-c", command, "/bin/sh", ...)
end
```

terminate(exitcode)
-------------------
Lets Lsyncd terminate with ```exitcode```.

event
-----
Variables of the actions are given by the _event_ field. It has following fields.

|Field|Meaning|
|:----|:----|
| event.config | the configuration as called with sync{} |
| event.inlet | see [layer 1](Layer 1 Config) about inlets |
| event.etype | the event type. Can be 'ATTRIB', 'CREATE', 'MODIFY', 'DELETE', 'MOVE' |
| event.status | the status of the event. 'wait' when it is ready to be spawned and 'active' if there is a process running associated with this event |
| event.isdir | true if the event relates to a directory |
|event.name | the filename, directories end with a slash |
| event.basename | the filename, directories do not end with a slash |
| event.path | see ^path of [Layer 3][l3-all-vars] |
|event.pathname | see ^pathname of [Layer 3][l3-all-vars] |
| event.source | see ^source of [Layer 3][l3-all-vars] |
| event.sourcePath | see ^sourcePath of [Layer 3][l3-all-vars] |
|event.sourcePathname | see ^sourcePathname of [Layer 3][l3-all-vars] |
|event.target | see ^target of [Layer 3][l3-all-vars] |
|event.targetPath | see ^targetPath of [Layer 3][l3-all-vars] |
|event.targetPathname | see ^targetPathname of [Layer 3][l3-all-vars] |
[l3-all-vars]:../wiki/Lsyncd-2.1.x-‖-Layer-3-Config-‖-Simple-onAction#all-possible-variables
onMove actions have two events as parameter, the origin and the destination of the move.

This example will tattle about all moves within the observed directory tree.

```Lua
tattleMove = {
    onMove = function(oEvent, dEvent)
        log("Normal", "A moved happened from ",
            oEvent.pathname, " to ",  dEvent.pathname)
    end,
}
```

Action functions have to be short and fast. They are running right within Lsyncd's one and only main thread. If you have to do any more time consuming calculations _spawn{}_ a child process instead. 

There can only be one child process associated to a event.

Layer 3 is nothing else than Lsyncd automatically write Layer 2 functions for you on initialization. Start Lsyncd with ```-log FWrite``` on a Layer 3 configuration to see what functions it dynamically writes and loads for you. Thus Layer 3 and 2 can also be be mixed at will.
