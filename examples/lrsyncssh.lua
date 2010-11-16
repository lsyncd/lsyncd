----
-- User configuration file for lsyncd.
--
-- Simple example for default rsync, but executing moves through on the target.
--
sync{default.rsyncssh, source="src", host="localhost", targetdir="dst/"}

