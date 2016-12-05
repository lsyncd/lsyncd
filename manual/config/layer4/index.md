---
layout: default
title: "Config Layer 4: Default Config"
short: "Config Layer 4" 
---
You can simply choose from a set of three default implementations which are: __rsync__, __rsyncssh__ and __direct__.

To sync a local directory using the default rsync behavior, just add this to a config file:

{% highlight lua %}
sync {
   default.rsync,
   source = "DIRNAME",
   target = "DIRNAME"
}
{% endhighlight %}

The order of the arguments is of no importance. If target is a local directory, take care that it is an absolute pathname. You can add multiple syncs that way. The source directories may be identical or differ without problems. ```source``` is an universal parameter that must be given for every sync. All other ```sync``` parameters can differ depending on the behavior selected. Optionally you can override the default or settings values ```maxDelays``` or ```maxProcesses``` per _Sync_. 

One can also skip the initial rsync process by setting the default ```init``` function to false:

{% highlight lua %}
sync {
    default.rsync,
    source = "DIRNAME",
    target = "DIRNAME",
    init   = false
}
{% endhighlight %}

This is an optimization which can be dangerous; so, please use it only if you are sure that source and target are synchronized when Lsyncd is started.

The default behaviors you can select from are following:

default.rsync
-------------
The default rsync configuration will aggregate events up to ```delay``` seconds or 1000 separate uncollapsible events, which ever happens first. Then it will spawn one Rsync with a filter of all files that  changed. The filter list is transmitted to Rsync trough a pipe. A call from Lsyncd to Rsync will thus look like this:

{% highlight shell %}
/usr/bin/rsync -ltsd --delete --include-from=- --exclude=* SOURCE TARGET
{% endhighlight %}

You can change the options Rsync is called and the Rsync binary that is call with the ```rsync``` parameter.

Example:

{% highlight lua %}
sync {
    default.rsync,
    source    = "/home/user/src/",
    target    = "foohost.com:~/trg/",
    delay     = 15, 
    rsync     = {
        binary   = "/usr/local/bin/rsync",
        archive  = true,
        compress = true
    }
}
{% endhighlight %}

Below is a table of options for the ```rsync``` parameter. Please have a look at the Rsync documentation for an in depth explanation.

<table>

 <tr><td> acls
</td><td> =
</td><td> true
</td><td>
</td></tr>

 <tr><td> append
</td><td> =
</td><td> true
</td><td> (Lsyncd >= 2.1.7)
</td></tr>

 <tr><td> append-verify
</td><td> =
</td><td> true
</td><td> (Lsyncd >= 2.1.7)
</td></tr>

 <tr><td> binary
</td><td> =
</td><td> FILENAME
</td><td> Lsyncd calls this binary as rsync (default: /usr/bin/rsync)
</td></tr>

 <tr><td> checksum
</td><td> =
</td><td> true
</td><td>
</td></tr>

 <tr><td> chmod
</td><td> =
</td><td> STRING
</td><td> (Lsyncd >= 2.1.7)
</td></tr>

 <tr><td> chown
</td><td> =
</td><td> USER:GROUP
</td><td> (Lsyncd >= 2.1.7)
</td></tr>

 <tr><td> compress
</td><td> =
</td><td> true
</td><td>
</td></tr>

 <tr><td> copy_dirlinks
</td><td> =
</td><td> true
</td><td> (Lsyncd >= 2.1.7)
</td></tr>

 <tr><td> copy_links
</td><td> =
</td><td> true
</td><td>
</td></tr>

 <tr><td> cvs_exclude
</td><td> =
</td><td> true
</td><td>
</td></tr>

 <tr><td> dry_run
</td><td> =
</td><td> true
</td><td>
</td></tr>

 <tr><td> exclude
</td><td> =
</td><td> PATTERN
</td><td> TABLE of PATTERNs also allowed
</td></tr>

 <tr><td> excludeFrom
</td><td> =
</td><td> FILENAME
</td><td>
</td></tr>

 <tr><td> executability
</td><td> =
</td><td> true
</td><td>
</td></tr>

 <tr><td> existing
</td><td> =
</td><td> true
</td><td> (Lsyncd >= 2.1.7)
</td></tr>

 <tr><td> groupmap
</td><td> =
</td><td> STRING
</td><td> (Lsyncd >= 2.1.7)
</td></tr>

 <tr><td> hard_links
</td><td> =
</td><td> true
</td><td>
</td></tr>

 <tr><td> ignore_times
</td><td> =
</td><td> true
</td><td>
</td></tr>

 <tr><td> inplace
</td><td> =
</td><td> true
</td><td> (Lsyncd >= 2.1.6)
</td></tr>

 <tr><td> ipv4
</td><td> =
</td><td> true
</td><td> 
</td></tr>

 <tr><td> ipv6
</td><td> =
</td><td> true
</td><td> 
</td></tr>

 <tr><td> links
</td><td> =
</td><td> true
</td><td> (set by Lsyncd by default)
</td></tr>

 <tr><td> one_file_system
</td><td> =
</td><td> true
</td><td>
</td></tr>

 <tr><td> owner
</td><td> =
</td><td> true
</td><td>
</td></tr>

 <tr><td> password_file
</td><td> =
</td><td> FILENAME
</td><td> (Lsyncd >= 2.1.2)
</td></tr>

 <tr><td> perms
</td><td> =
</td><td> true
</td><td>
</td></tr>

 <tr><td> protect_args
</td><td> =
</td><td> true
</td><td> (set by Lsyncd by default)
</td></tr>

 <tr><td> prune_empty_dirs
</td><td> =
</td><td> true
</td><td>
</td></tr>

 <tr><td> quiet
</td><td> =
</td><td> true
</td><td>
</td></tr>

 <tr><td> rsh
</td><td> =
</td><td> COMMAND
</td><td> 
</td></tr>

 <tr><td> rsync_path
</td><td> =
</td><td> PATH
</td><td> (path to rsync on remote host)
</td></tr>

 <tr><td> sparse
</td><td> =
</td><td> true
</td><td>
</td></tr>

 <tr><td> temp_dir
</td><td> =
</td><td> DIR
</td><td>
</td></tr>

 <tr><td> times
</td><td> =
</td><td> true
</td><td> (set by Lsyncd by default)
</td></tr>

 <tr><td> update
</td><td> =
</td><td> true
</td><td>
</td></tr>

 <tr><td> usermap
</td><td> =
</td><td> STRING
</td><td> (Lsyncd >= 2.1.7)
</td></tr>

 <tr><td> verbose
</td><td> =
</td><td> true
</td><td>
</td></tr>

 <tr><td> whole_file
</td><td> =
</td><td> true
</td><td>
</td></tr>

 <tr><td> xattrs
</td><td> =
</td><td> true
</td><td>
</td></tr>

 <tr><td> _extra
</td><td> =
</td><td> TABLE of STRINGS.
</td><td> If absolutely needed, additional arguments can be specified as a TABLE of STRINGS(example: <tt>{ "--omit-dir-times", "--omit-link-times" }</tt>). Note that the underscore highlights this as workaround. If you need something that is not covered by the above options, please request it via a feature request on the project website. Most notably, do not add -r for recursive or -a which implies recursive, since Lsyncd will handle that by itself. Additionally do not add -R for relative, which will ruin Lsyncd &lt;-&gt; Rsync communication.
</td></tr>

</table>

default.rsyncssh
----------------
This configuration differs from the standard rsync configuration in that it uses ssh commands to move files or directories locally at the target host instead of deleting and transferring again. This configuration does spawn Rsync processes like default.rsync but additionally will spawn ```/usr/bin/ssh HOST mv ORIGIN DESTINATION``` commands. 

Different to default.rsync it does not take an uniform ```target``` parameter, but needs ```host``` and ```targetdir``` separated.

Rsync's options can be changed with the ```rsync``` parameter like in default.rsync described above.

Additional to that ssh can be configured via the ```ssh``` parameter.

<table>

 <tr><td> binary
</td><td> =
</td><td> FILENAME
</td><td> Lsyncd calls this binary as ssh (default: /usr/bin/ssh)
</td></tr>

 <tr><td> identityFile
</td><td> =
</td><td> FILE
</td><td> Uses this file to identify for public key authentication.
</td></tr>

 <tr><td> options
</td><td> =
</td><td> TABLE
</td><td> A table of addition extended options to pass to ssh's -o option.
</td></tr>

 <tr><td> port
</td><td> =
</td><td> PORT
</td><td> Adds --port=PORT to the ssh call.
</td></tr>

 <tr><td> _extra
</td><td> =
</td><td> STRING TABLE
</td><td> Similar to rsync._extra this can be used as quick workaround if absolutely needed.
</td></tr>

</table>

Example:

{% highlight lua %}
settings {
    logfile = "/var/log/lsyncd.log",
    statusFile = "/var/log/lsyncd-status.log",
    statusInterval = 20
}

sync {
   default.rsyncssh,
   source="/srcdir",
   host="remotehost",
   excludeFrom="/etc/lsyncd.exclude",
   targetdir="/dstdir",
   rsync = {
     archive = true,
     compress = false,
     whole_file = false
   },
   ssh = {
     port = 1234
   }
}
{% endhighlight %}

Please note the comma between the ```rsync``` parameter set and the ```ssh``` parameter set.

**Caution**
If you are upgrading from 2.0.x, please notice that `settings` became a function from a variable, so you **MUST** delete the equal sign '=' between `settings` and the `{`.

Lsyncd will call ```xargs``` on the remote host to handle multiple tasks in a single connection. Xargs options can be specified by the xargs parameter.

<table>

 <tr><td> binary
</td><td> =
</td><td> FILENAME
</td><td> Lsyncd calls this binary as xargs on the remote host (default: /usr/bin/xargs)
</td></tr>

 <tr><td> delimiter
</td><td> =
</td><td> DELIMITER
</td><td> delimiting character to separate filenames. By default the 0 character is used. Very old holds may need newline instead.
</td></tr>

 <tr><td> _extra
</td><td> =
</td><td> STRING TABLE
</td><td> By default { '-0', 'rm -rf' }. Remove the -0 if you chose newline delimiter instead. Otherwise leave it as is.
</td></tr>

</table>


Example:

{% highlight lua %}
sync {
    default.rsyncssh,
    source    = "/home/user/src/",
    host      = "foohost.com",
    targetdir = "~/trg/",
}
{% endhighlight %}

default.direct
-------------
Default.direct can be used to keep two local directories in sync with better performance than using default.rsync. Default.direct uses (just like default.rsync) rsync on startup to initially synchronize the target directory with the source directory. However, during normal operation default.direct uses /bin/cp, /bin/rm and /bin/mv to keep the synchronization. All parameters are just like default.rsync.

Example:

{% highlight lua %}
sync {
    default.direct,
    source  = "/home/user/src/",
    target  = "/home/user/trg/"
}
{% endhighlight %}

Exclusions
----------
Two additional parameters can be specified to sync{}:

<table>

 <tr><td> excludeFrom
</td><td> =
</td><td> FILENAME
</td><td> loads exclusion rules from this file, on rule per line
</td></tr>

 <tr><td> exclude
</td><td> =
</td><td> LIST
</td><td> loads exclusion rules from this list of strings
</td></tr>

</table>

Exclusion rules are modeled after rsync's exclusion patterns but are a bit simpler. Lsyncd supports these features:

 * Generally if any part of the pathname (see below Layer 3) of an event matches the text, it is excluded. E.g. the file "/bin/foo/bar" matches the rule "foo".
 * If the rule starts with a slash, it will only be matched at the beginning of the pathname
 * If the rule ends with a slash, it will only be matched at the end of a pathname
 * ? matches any character that is not a slash.
 * ```*``` matches zero or more characters that are not a slash
 * ```**``` matches zero or more characters, this can be slashes.

Example:

{% highlight lua %}
sync {
    default.rsync,
    source    = "/home/user/src/",
    targetdir = "/home/user/dst/",
    exclude = { '.bak' , '.tmp' }
}
{% endhighlight %}

Deletions
---------
By default Lsyncd will delete files on the target that are not present at the source since this is a fundamental part of the idea of keeping the target in sync with the source. However, many users requested exceptions for this, for various reasons, so all default implementations take ```delete``` as an additional parameter. 

Valid values for ```delete``` are:

<table>

 <tr><td> delete
</td><td> =
</td><td> true
</td><td> Default. Lsyncd will delete on the target whatever is not in the source. At startup and what's being deleted during normal operation.
</td></tr>

 <tr><td> delete
</td><td> =
</td><td> false
</td><td> Lsyncd will not delete any files on the target. Not on startup nor on normal operation. (Overwrites are possible though)
</td></tr>

 <tr><td> delete
</td><td> =
</td><td> 'startup'
</td><td> Lsyncd will delete files on the target when it starts up but not on normal operation.
</td></tr>

 <tr><td> delete
</td><td> =
</td><td> 'running'
</td><td> Lsyncd will not delete files on the target when it starts up but will delete those that are removed during normal operation.
</td></tr>

</table>
