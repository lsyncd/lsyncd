---
layout: default
title: "FAQ: How can I sync from one source to multiple targets?"
---
If you got multiple targets, you simple specify the sync command multiple times.

{% highlight lua %}
sync{ default.rsync, source='/sourcedir', target='targethost1:/targetdir' }
sync{ default.rsync, source='/sourcedir', target='targethost2:/targetdir' }
sync{ default.rsync, source='/sourcedir', target='targethost3:/targetdir' }
{% endhighlight %}

Lsyncd will notice multiple uses of the same source directory or the use of a subdirectory of an already used source directory and creates only watch per subdirectoy watched in any sync.

To remedy the multiplication of the same configuration you can even use a loop to configure multiple targets.

This is the same configuration as before using a loop:

{% highlight lua %}
targets = {
    'targethost1:/targetdir',
    'targethost2:/targetdir',
    'targethost3:/targetdir',
}

for _, target in ipairs( targets )
do
    sync{ default.rsync, source='/sourcedir', target=target }
end
{% endhighlight %}

## [‹‹ back to FAQ index](../)
