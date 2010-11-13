----
-- User configuration file for lsyncd.
--
-- Simple example for default rsync.
--
settings = {
	statusFile = "/tmp/lsyncd.stat",
	statusIntervall = 1,
}

sync{default.rsync, source="src", target="mooooooooooooooooooooo.com:dst/", excludeFrom="exclude"}

