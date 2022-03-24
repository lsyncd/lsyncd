----
-- User configuration file for lsyncd.
--
-- Advanced example for rsync via ssh tunnel.
--
settings {
    logfile = "/tmp/lsyncd/lsyncd.log",
    statusFile = "/tmp/lsyncd/lsyncd.status",
    insist = true
}

sync {
    default.rsync,
    tunnel = tunnel {
         command = {"ssh", "-N", "-L", 
"localhost:1873:localhost:873", "lsyncd-proxy@server-test-ubu"}
    },
    source = "/tmp/src",
    target    = "rsync://localhost:1873/test",
    delay = 5,
    rsync = {
            verbose = true,
            inplace = true,
            _extra = {
                    "--info=progress2"
            }
    }
}

----
-- the rsyncd.conf on the receiver side:
--
-- uid = root
-- gid = root
-- use chroot = yes
-- max connections = 4
-- syslog facility = local5
-- pid file = /var/run/rsyncd.pid

-- [test]
-- path = /home/poelzi/test
-- read only = false