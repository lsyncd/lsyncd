---
layout: default3
title: Building
---

## Requirements

### Lua >= 5.2
Lsyncd depends on Lua 5.2 or greater; that is 5.2 or 5.3. For most distributions you need to install the liblua??, the liblua??-dev and the lua?? package, with ?? being the respective Lua version.

### cmake >= 2.8

To configure Lsyncd to your system, cmake >= 2.8 is required

### rsync >= 3.1
During runtime Lsyncd needs rsync > 3.1 installed both on source and target systems.

## Compiling

With these requirements fulfilled building Lsyncd should be a straight forward process. Unpack the downloaded tar.gz file and run:

{% highlight shell %}
cmake .
make
sudo make install
{% endhighlight %}
