---
layout: default
title: Compiling
---

# Requirements

Lsyncd depends on Lua 5.1 or greater. that is Lua 5.1, 5.2 or 5.3. For later Lua versions you need an update Lsnycd version. For most distributions you need the liblua??, the liblua??-dev and the lua?? package, with ?? being the Lua version.

# Compiling

Building Lsyncd should be a straight forward process. Unpack the downloaded tar.gz file and run:

{% highlight shell %}
cmake .
make
sudo make install
{% endhighlight %}
