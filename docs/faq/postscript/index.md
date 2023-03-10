---
layout: default
title: "FAQ: How can I call a script before or after each rsync operation?"
---
The issue with this quite frequent request is, by itself it complicates error handling a lot. What should Lsyncd do, when the script fails that it ought to run after each rsync call? If it should recall the post script it would require a new state for each rsync event which would complicate Lsyncd code quite a bit.

The easiest way to get around this, is by replacing the rsync binary Lsyncd calls by a script from you, that calls rsync and does whatever you want to do, when rsync completes. The only thing to take care is that Lsyncd communicates with rsync using stdin/out/err-pipes and thus better not interfere with these.

Also take care the script properly forwards the exit code rsync returned.

This is an example bash script to wrap around rsync:

{% highlight shell %}
#!/bin/bash
/usr/bin/rsync "$@"
result=$?
(
  if [ $result -eq 0 ]; then
     echo "my commands";
  fi
) >/dev/null 2>/dev/null </dev/null

exit $result
{% endhighlight %}

It does not do error handling for post commands. If you need this, you'll have to code them here fitting your requirements.

Above script can be used as rsync wrapper replacement like this:


{% highlight lua %}
sync {
    default.rsync, 
    source = "/path/to/source", 
    target = "targethost::targetdir", 
    rsync = {
        binary = "/path/to/bash/handler.sh"
    }
}
{% endhighlight %}

If you want to track directories and files passed to rsync command (--include-from) in your bash script, consider that they come via STDIN and NUL delimited chars (rsync is called using --include-from=- --from0).

After calling rsync within your script, STDIN is lost, so if you need it later could do something like this:

{% highlight shell %}
#!/bin/bash

#create unique temp file to store incoming STDIN
_TMP_FILE=$(mktemp -q /tmp/lsd-rsync.XXXXXXXXXX || exit 1)

#Set trap to automatically clean up the temp file on exit
trap 'rm -f -- "$_TMP_FILE"' EXIT

#get last argument (TARGET) and next to last (SOURCE)
_SOURCE=${@: -2:1}
_TARGET=${@: -1:1}

#save the current STDIN with \0 (NUL) chars to a file, or we'll lose the original STDIN after calling rsync
cat - >$_TMP_FILE

#execute rsync with arguments and inject STDIN with NUL delimited chars
/usr/bin/rsync "$@" <$_TMP_FILE

result=$?
(
    # ...
    # Do anything you need with our saved STDIN as $_TMP_FILE, $_SOURCE, $_TARGET, etc.
    # ...
    # for example
    # replace \0 (NUL) chars with tabs in our stdin saved as a file
    # sed -i 's/\x0/\t/g' $_TMP_FILE
    # pass this new file to other script, etc.
)

{% endhighlight %}

## [‹‹ back to FAQ index](../)
