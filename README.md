Lsyncd -- Live Syncing (Mirror) Daemon
======================================
Description
-----------
Lsyncd watches a local directory trees event monitor interface (inotify or fsevents). It aggregates and combines events for a few seconds and then spawns one (or more) process(es) to synchronize the changes. By default this is [rsync](http://rsync.samba.org/).  Lsyncd is thus a light-weight live mirror solution that is comparatively easy to install not requiring new filesystems or block devices and does not hamper local filesystem performance.

Rsync+ssh is an advanced action configuration that uses a SSH to act file and directory moves directly on the target instead of re-transmitting the move destination over the wire.

Fine-grained customization can be achieved through the config file.  Custom action configs can even be written from scratch in cascading layers ranging from shell scripts to code written in the [Lua language](http://www.lua.org/). This way simple, powerful and flexible configurations can be acheived.  See [the manual](https://axkibe.github.io/lsyncd/) for details.

Lsyncd 2.2.1 requires rsync >= 3.1 on all source and target machines.

License: [GPLv2](http://www.fsf.org/licensing/licenses/info/GPLv2.html) or any later GPL version.

When to use
-----------
Lsyncd is designed to synchronize a local directory tree with low profile of expected changes to a remote mirror. Lsyncd is especially useful to sync data from a secure area to a not-so-secure area.

Other synchronization tools
------------------------
[DRBD](http://www.drbd.org) operates on block device level. This makes it useful for synchronizing systems that are under heavy load. Lsyncd on the other hand does not require you to change block devices and/or mount points, allows you to change uid/gid of the transferred files, separates the receiver through the one-way nature of rsync. DRBD is likely the better option if you are syncing databases.

[GlusterFS](http://www.gluster.org) and [BindFS](http://bindfs.org/) use a FUSE-Filesystem to interject kernel/userspace filesystem events.

[Mirror](https://github.com/stephenh/mirror) is an asynchronous synchronisation tool that takes use of the inotify notifications much like Lsyncd. The main differences are: it is developed specifically for master-master use, thus running on a daemon on both systems, uses its own transportation layer instead of rsync and is Java instead of Lsyncd's C core with Lua scripting.

Lsyncd usage examples
---------------------
```lsyncd -rsync /home remotehost.org::share/```

This watches and rsyncs the local directory /home with all sub-directories and
transfers them to 'remotehost' using the rsync-share 'share'.

```lsyncd -rsyncssh /home remotehost.org backup-home/```

This will also rsync/watch '/home', but it uses a ssh connection to make moves local on the remotehost instead of re-transmitting the moved file over the wire.

Some more complicated examples, tips and tricks you can find in the [manual](https://axkibe.github.io/lsyncd/).

Disclaimer
----------
Besides the usual disclaimer in the license, we want to specifically emphasize that the authors, and any organizations the authors are associated with, can not be held responsible for data-loss caused by possible malfunctions of Lsyncd.

