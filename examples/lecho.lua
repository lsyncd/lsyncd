-----
-- User configuration file for lsyncd.
--
-- This example uses local bash commands to keep two local
-- directory trees in sync.
--

-----
-- for testing purposes. just echos what is happening.
--
echo = {
	maxProcesses = 1,
	delay = 1,
	onStartup = "/bin/echo telling about ^source",
	onAttrib  = "/bin/echo attrib ^pathname",
	onCreate  = "/bin/echo create ^pathname",
	onDelete  = "/bin/echo delete ^pathname",
	onModify  = "/bin/echo modify ^pathname",
	onMove    = "/bin/echo move ^o.pathname -> ^d.pathname",
}

sync{echo, source="src", target="/path/to/trg/"}

