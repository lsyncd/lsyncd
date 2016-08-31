-----
-- User configuration file for lsyncd.
--
-- This example uses local bash commands to keep two local
-- directory trees in sync.
--
settings {
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
	delay = 0,

	maxProcesses = 1,

	-- calls `cp -r SOURCE/* TARGET` only when there is something in SOURCE
	-- otherwise it deletes contents in the target if there.
	onStartup = [[
if [ "$(ls -A ^source)" ]; then
	cp -r ^source* ^target;
else
	if [ "$(ls -A ^target)" ]; then rm -rf ^target/*; fi
fi]],

	onCreate = prefix..[[cp -r ^sourcePath ^targetPathdir]],

	onModify = prefix..[[cp -r ^sourcePath ^targetPathdir]],

	onDelete = prefix..[[rm -rf ^targetPath]],

	onMove   = prefix..[[mv ^o.targetPath ^d.targetPath]],
}

sync{bash, source="src", target="/path/to/trg/"}

