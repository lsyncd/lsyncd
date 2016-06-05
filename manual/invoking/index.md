---
layout: default
title: Invoking
---
As most Unix tools, Lsyncd will print a synopsis of its command line options when called with --help. 

```shell
lsyncd --help
lsyncd -help
```

The two hyphens are redundant for Lsyncd. It has no short one letter options and one hyphen will always result into the same as specifying two.

Also like most Unix tools, --version or -version will let Lsyncd print its version number.

```
lsyncd -version
```

Lsyncd 2.1 is designed to be predominantly configured through a config file (see below). The config file can thus be the only command line option.

```shell
lsyncd CONFIGFILE
```

Although for standard use or quick testing it can be cursorily configured by command line options. The following will keep a local source and destination directory in sync using rsync:

```shell
lsyncd -rsync /home/USER/src /home/USER/dst
```

The target can here be anything that Rsync recognizes.

```shell
lsyncd -rsync /home/USER/src remotehost:dst
```

Two (or more) targets are configured by calling -rsync twice (or several times).

```shell
lsyncd -rsync /home/USER/src remotehost1:dst -rsync /home/USER/src remotehost2:dst 
```

A disadvantage with Rsync synchronization is that normally directory and file moves result in a deletion of the move origin and a retransfer of the move destination of the wire. However, Lsyncd 2 can use ssh commands to move the directories and files locally on the target. To use this use ```-rsyncssh``` followed by the local source directory, the remote host and the target directory there. The REMOTEHOST can include a user like ```me@remotehost.com```.

```shell
lsyncd -rsyncssh /home/USER/src REMOTEHOST TARGETDIR
```

When testing Lsyncd configurations ```-nodaemon``` is a pretty handy flag. With this option, Lsyncd will not detach and will not become a daemon. All log messages are additionally to the configured logging facilities printed on the console (_stdout_ and _stderr_). 

```shell
lsyncd -nodaemon CONFIGFILE
```

Note there is a difference in behaviour. When running with -nodaemon, Lsyncd will not change its working directory to ```/```, as it does when becoming a daemon. Thus relative targets like ```./target``` will work with ```-nodaemon```, but must be specified to absolute paths to work in daemon mode. The source directories will also be turned into absolute paths by Lsyncd. The reason targets are not resolved to absolute paths while sources are is because Lsyncd itself does not care about the format of the target specifier which can also be remote hosts, rsyncd modules, etc. It is opaquely handed to rsync. It cares about the observed directories though.

All log messages are sorted in categories. By default Lsyncd is scarce with log messages. You can turn Lsyncd into a motormouth by specifying ```-log all```.

```shell
lsyncd -log all CONFIGFILE
```

This might easily become too much. A particularly useful category is "Exec" which will log the command lines of all processes Lsyncd spawns.

```shell
lsyncd -log Exec CONFIGFILE
```
