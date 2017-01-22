---
layout: default
title: "The Configuration File"
short: "Config File" 
---
Lsyncd configuration files are valid [Lua syntax](http://www.lua.org/). It is designed to be simple yet potent. While rich configuration and simplicity are not opposites by themselves, some trade-offs are inevitable. To achieve both goals as far as possible, Lsyncd configuration can be done at different layers. Lower layers add adaptability while the interface becomes more engaging.

Settings
--------
For scripts of all layers, the ```settings``` call can be used to alter daemon-wide configurations.

For example, the following code will instruct Lsyncd to log into ```/tmp/lsyncd.log```, periodically update the file ```/tmp/lsyncd.status``` with its status and to not detach as a daemon.

{% highlight lua %}
settings {
   logfile    = "/tmp/lsyncd.log",
   statusFile = "/tmp/lsyncd.status",
   nodaemon   = true,
}
{% endhighlight %}

**Caution**
If you are upgrading from 2.0.x, please notice that `settings` became a function from a variable, so you **MUST** delete the equal sign '=' between `settings` and the `{`.

Valid keys for settings are:

<table>

 <tr><td> logfile
</td><td> =
</td><td> FILENAME
</td><td> logs into this file
</td></tr>

 <tr><td> pidfile
</td><td> =
</td><td> FILENAME
</td><td> logs PID into this file
</td></tr>

 <tr><td> nodaemon
</td><td> =
</td><td> true
</td><td> does not detach
</td></tr>

 <tr><td> statusFile
</td><td> =
</td><td> FILENAME
</td><td> periodically writes a status report to this file
</td></tr>

 <tr><td> statusInterval
</td><td> =
</td><td> NUMBER
</td><td> writes the status file at shortest after this number of seconds has passed (default: 10)
</td></tr>

 <tr><td> logfacility
</td><td> =
</td><td> STRING
</td><td> syslog facility, default "user"
</td></tr>

 <tr><td> logident
</td><td> =
</td><td> STRING
</td><td> syslog identification (tag), default "lsyncd"
</td></tr>

 <tr><td> insist
</td><td> =
</td><td> true
</td><td> keep running at startup altough one or more targets failed due to not being reachable.
</td></tr>

 <tr><td> inotifyMode
</td><td> =
</td><td> STRING
</td><td> Specifies on inotify systems what kind of changes to listen to. Can be "Modify", "CloseWrite" (default) or "CloseWrite or Modify".
</td></tr>

 <tr><td> maxProcesses
</td><td> =
</td><td> NUMBER
</td><td> Lysncd will not spawn more than these number of processes. This adds across all sync{}s.
</td></tr>

</table>

Additionally some parameters can be configured, which are inherited by all _Syncs_ (see Layer 3)

<table>
 <tr><td> maxDelays
</td><td> =
</td><td> NUMBER
</td><td> When this amount of delayed events is queued, actions will be spawned, even below the delay timer.
</td></tr>
</table>
