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

	onStartup = 
		[[if [ "$(ls -A $1)" ]; then cp -r ^source* ^target; fi]],

	onCreate = prefix..[[cp -r ^sourcePathname ^targetPathname]],
	
	onModify = prefix..[[cp -r ^sourcePathname ^targetPathname]],
	
	onDelete = prefix..[[rm -rf ^targetPathname]],

	onMove   = prefix..[[mv ^o.targetPathname ^d.targetPathname]],
}

sync{bash, source="src", target="dst/"}

