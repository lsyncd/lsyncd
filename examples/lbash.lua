-----
-- User configuration file for lsyncd.
-- 
-- This example uses local bash commands to keep two local 
-- directory trees in sync.
--
settings = {
	logfile         = "/tmp/lsyncd.log",
	statusFile      = "/tmp/lsyncd.stat",
	statusIntervall = 1,
	nodaemon        = true,
}

-----
-- for testing purposes. prefix can be used to slow commands down.
-- prefix = "sleep 5 && "
--
prefix = ""

-----
-- for testing purposes. uses bash command to hold local dirs in sync.
--
bash = {

	maxProcesses = 3,

	onStartup = 
		[[if [ "$(ls -A ^source)" ]; then cp -r ^source* ^target; fi]],

	onCreate = prefix..[[cp -r ^sourcePathname ^targetPathname]],
	
	onModify = prefix..[[cp -r ^sourcePathname ^targetPathname]],
	
	onDelete = prefix..[[rm -rf ^targetPathname]],

	onMove   = prefix..[[mv ^o.targetPathname ^d.targetPathname]],
}

sync{bash, source="src", target="/path/to/trg/"}

