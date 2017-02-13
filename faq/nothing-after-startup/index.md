---
layout: default
title: "FAQ: The startup sync works but after that Lsyncd doesn't do anything!"
---
This almost always caused by the fact you specified a network mounted directory as source.

Lsyncd requires the kernels inotify or fsevents interface to get noted of file changes. No known network filesystem (known to Lsyncd authors) supports forwarding file notifcations events.

Thus Lsyncd needs to run on the system where the files are located physically.

## [‹‹ back to FAQ index](../)
