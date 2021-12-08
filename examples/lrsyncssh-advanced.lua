settings {
    logfile = "/home/lsync/job1/lsyncd.log",
    statusFile = "/home/lsync/job1/lsyncd.status",
    insist = true
}

sync {
    default.rsyncssh,
    source = "/data/projects",
    host = "offsitehost",
    targetdir = "/data/projects",
    excludeFrom = "/home/lsync/job1/lsyncd.exclude",
    delay = 5,
    rsync = {
            verbose = true,
            inplace = true,
            _extra = {
                    "--info=progress2"
            }
    },
    ssh = {
            identityFile = "/home/lsync/.ssh/id_rsa_new",
            options = {
                    User = "poelzi",
                    StrictHostKeyChecking = "no",
                    Compression = "no",
                    Cipher = "aes256-gcm@openssh.com"
            },
            _extra = {
                    "-T",
                    "-c",
                    "aes256-gcm@openssh.com"
            }
    }
}