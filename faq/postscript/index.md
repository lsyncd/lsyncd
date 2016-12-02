---
layout: default
title: "FAQ: How can I call a script after each rsync operation?"
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
