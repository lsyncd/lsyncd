---
layout: default
title: "Lsyncd - Live Syncing (Mirror) Daemon"
short: "Welcome"
---
Description
-----------
Lsyncd uses a filesystem event interface (inotify or fsevents) to watch for changes to local files and directories.  Lsyncd collates these events for several seconds and then spawns one or more processes to synchronize the changes to a remote filesystem.  The default synchronization method is [rsync](http://rsync.samba.org/).  Thus, Lsyncd is a light-weight live mirror solution.  Lsyncd is comparatively easy to install and does not require new filesystems or block devices.  Lysncd does not hamper local filesystem performance.

As an alternative to rsync, Lsyncd can also push changes via rsync+ssh.  Rsync+ssh allows for much more efficient synchronization when a file or directory is renamed or moved to a new location in the local tree.  (In contrast, plain rsync performs a move by deleting the old file and then retransmitting the whole file.)

Fine-grained customization can be achieved through the config file.  Custom action configs can even be written from scratch in cascading layers ranging from shell scripts to code written in the [Lua language](http://www.lua.org/). Thus, simple, powerful and flexible configurations are possible.

Lsyncd 2.2.1 requires rsync >= 3.1 on all source and target machines.

License: [GPLv2](http://www.fsf.org/licensing/licenses/info/GPLv2.html) or any later GPL version.

When to use
-----------
Lsyncd is designed to synchronize a slowly changing local directory tree to a remote mirror.  Lsyncd is especially useful to sync data from a secure area to a not-so-secure area.

Other synchronization tools
------------------------
[DRBD](http://www.drbd.org) operates on block device level. This makes it useful for synchronizing systems that are under heavy load. Lsyncd on the other hand does not require you to change block devices and/or mount points, allows you to change uid/gid of the transferred files, and separates the receiver through the one-way nature of rsync. DRBD is likely the better option if you are syncing databases.

[GlusterFS](http://www.gluster.org) and [BindFS](http://bindfs.org/) use a FUSE-Filesystem to interject kernel/userspace filesystem events.

[Mirror](https://github.com/stephenh/mirror) is an asynchronous synchronization tool that makes use of the inotify notifications much like Lsyncd. The main differences are: it is developed specifically for master-master use, thus running on a daemon on both systems, uses its own transportation layer instead of rsync and is Java instead of Lsyncd's C core with Lua scripting.

Lsyncd usage examples
---------------------
{% highlight shell %}
lsyncd -rsync /home remotehost.org::share/
{% endhighlight %}

This watches and rsyncs the local directory /home with all sub-directories and
transfers them to 'remotehost' using the rsync-share 'share'.

{% highlight shell %}
lsyncd -rsyncssh /home remotehost.org backup-home/
{% endhighlight %}

This will also rsync/watch '/home', but it uses a ssh connection to make moves local on the remotehost instead of re-transmitting the moved file over the wire.

Disclaimer
----------
Besides the usual disclaimer in the license, we want to specifically emphasize that neither the authors, nor any organization associated with the authors, can or will be held responsible for data-loss caused by possible malfunctions of Lsyncd.
