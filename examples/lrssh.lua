----
-- User configuration file for lsyncd.
--
-- Simple example for default rsync.
--
settings = {
	statusFile = "/tmp/lsyncd.stat",
	statusIntervall = 1,
}

sync{default.rssh, source="src", host="localhost", targetdir="dst/"}

