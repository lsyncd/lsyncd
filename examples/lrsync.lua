----
-- User configuration file for lsyncd.
--
-- Simple example for default rsync.
--
settings {
	statusFile = "/tmp/lsyncd.stat",
	statusInterval = 1,
}

sync{
	default.rsync,
	source="src",
	target="trg",
}

