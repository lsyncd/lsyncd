---
layout: default
title: "Config Layer 3: Simple onAction"
short: "Config Layer 3" 
---
Simple onAction
---------------
In this layer, custom configurations can be created. This example will use bash commands to keep a local directory in sync.

```Lua
bash = {
    delay = 5,
    maxProcesses = 3,
    onCreate = "cp -r ^sourcePathname ^targetPathname",
    onModify = "cp -r ^sourcePathname ^targetPathname",
    onDelete = "rm -rf ^targetPathname",
    onMove   = "mv ^o.targetPathname ^d.targetPathname",
    onStartup = '[[ if [ "$(ls -A ^source)" ]; then cp -r ^source* ^target; fi]]',
}
```

The example explained step by step. Technically, any Lsyncd configuration is a Lua table with a set of keys filled out. Thus it starts by creating a variable called ```bash``` and assigns it a table with = { ... }.

```Lua
bash = {
  ...
}
```

Now the table is filled with entries. Every entry having a key left of the equal sign and its value right of it. If no delay is specified, this means immediate actions for Lsyncd. This example wants to aggregate changes for 5 seconds thus the next entry is:

```Lua
    delay = 5,
```

And a comma is needed since to mark the end of an entry.

Actions
-------
Actions are specified by the 6 keys: 

<table>

 <tr><td> onAttrib
</td><td> called when only attributes changed
</td></tr>

 <tr><td> onCreate
</td><td> called on a new file or directory
</td></tr>

 <tr><td> onModify
</td><td> called when a file has changed
</td></tr>

 <tr><td> onDelete
</td><td> called when a file or directory has been deleted
</td></tr>

 <tr><td> onMove
</td><td> called when a file or directory has been moved within the observed directory tree
</td></tr>

 <tr><td> onStartup
</td><td> called on the start of Lsyncd
</td></tr>

</table>

When there is no ```onMove``` or the move goes into or out of the observed directory tree, it is split into an ```onDelete``` of the move origin and an ```onCreate``` of the move destination. That is if either is within the observed directory tree. ```onStartup``` will always block all other actions for this _Sync_ until completed.

The action to be taken is specified as a Lua string. Thus actions can be delimited with anything Lua allows, these are 'TEXT', "TEXT", or '[[TEXT]] as used in ```onStartup``` in the example above. 

Any action starting with a "/" instructs Lsyncd to directly call the binary file at the beginning instead of spawning an additional shell. For example

```Lua
   onCreate = "/usr/bin/zip /usr/var/all.zip ^sourceName"
   onModify = "/usr/bin/zip /usr/var/all.zip ^sourceName"
```

will add any newly created and modified files to /usr/var/all.zip using absolute path names. Any action not starting with a "/" will result in Lsyncd spawning a shell to execute the action as command.

Variables
---------
Variable arguments are specified with the caret symbol ^. It has been chosen over $ or other symbols to be less conflicting with standard shell conventions. 

Note that variables will always be implicitly quoted in double quotes, so if you want them to be a part of another double-quoted string, you will have to go one layer deeper, e.g. 

```Lua
    onCreate   = '[[ su user -c "/usr/bin/zip /usr/var/all.zip ^o.sourceName " ]],
```

will expand to ```su user -c "/usr/bin/zip /usr/var/all.zip "source""``` which is incorrect and will break. You have to rewrite the above statement one layer deeper as 

```Lua
  onCreate = function(event)
    spawnShell('[[ su user -c "/usr/bin/zip /usr/var/all.zip \"$1\"" ]], event.sourceName)
  end
```


All possible variables
----------------------
<table>

 <tr><td> ^source
</td><td> the absolute path of the observed source directory
</td></tr>

 <tr><td> ^target
</td><td> the "target" attribute of the config
</td></tr>

 <tr><td> ^path
</td><td> the relative path of the file or directory to the observed directory; directories have a slash at the end.
</td></tr>

 <tr><td> ^pathname
</td><td> the relative path of the file or directory to the observed directory; directories have no slash at the end.
</td></tr>

 <tr><td> ^sourcePath
</td><td> the absolute path of the observed source directory and the relative path of the file or directory; this equals the absolute local path of the file or directory. Directories have a slash at the end.
</td></tr>

 <tr><td> ^sourcePathname
</td><td> same as ^sourcePath, but directories have no slash at the end.
</td></tr>

 <tr><td> ^targetPath
</td><td> The "target" attributed of the config appended by the relative path of the file or directory. Directories have a slash at the end.
</td></tr>

 <tr><td> ^targetPathname
</td><td> same as ^targetPath, but directories have no slash at the end.
</td></tr>

</table>

For ```onMoves``` a _o._ and or _d._ can be prepended to path, pathname, sourcePath sourcePathname, targetPath and targetPathname to specify the move origin or destination. Without neither the variables refers to the move origin. 

From the example above, it moves the file or directory in the target directory.
```
    onMove   = "mv ^o.targetPathname ^d.targetPathname",
```

Execution control (exit codes)
------------------------------
A few words on the startup of the example. It looks a little more complicated, but it is just some bash scripting, nothing Lsyncd specific. It simply does a recursive copy of the source to the target, but first tests if there is anything in the source file. Otherwise the command returns a non-zero error code.

```Lua
    onStartup = '[[if [ "$(ls -A ^source)" ]; then cp -r ^source* ^target; fi]],
```

By default Lsyncd ignores all exit codes except onStartup which must return 0 for it to continue. You can change this behavior by adding a ```exitcodes``` table.

```Lua
    exitcodes = {[0] = "ok", [1] = "again", [2] = "die"}
```
The keys specify for the exit code the string of the desired action. 

<table>

 <tr><td> again
</td><td> respawns the action after {{delay}} seconds, or 1 second if delay is immediate
</td></tr>

 <tr><td> die
</td><td> lets Lsyncd terminate.
</td></tr>

</table>

All other values let Lsyncd continue normally.
