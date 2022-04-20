-- This is an advanced tunnel config that uses the tunnel, load balancing
-- and extra large file transfers

sync {
    default.rsync,
    tunnel = tunnel {
        command = {"ssh", "-N", "-L", "localhost:${localport}:localhost:873", "user@testmachine"},
        mode = "pool",
        parallel = 2,
    },
    crontab = {
            -- does a full sync once a day at 3:00:01
            "1 0 3 * * *"
    },
    source = "/data/projects",
    target    = "rsync://localhost:${localport}/projects",
    delay = 5,
    batchSizeLimit = 1024 * 1024 * 30,
    maxProcesses = 4,
    rsync = {
            inplace = true,
    }
}

-- On your target machine configure rsyncd.conf like this:
-- [projects]
--   uid = myuser
--   gid = mygroup
--   path = /srv/projects
--   read only = false

-- If you restrict the ssh key or server to allow only port forwarding and no shell
-- this is a very secure setup