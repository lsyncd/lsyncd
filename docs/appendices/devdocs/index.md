---
layout: default
title: Developer Docs
short: Developer Docs
---
Code structure / Style guide
----------------------------
At large, Lsyncd's documentation contained in source code comments is well developed - at least compared to many other software projects. Here thus only a few outlining words.

Lsyncd 2 is partially written in C, partially written in Lua. For some code pieces it is a free decision in which it could be coded, but for most one of the two was more appropriate. Generally the goal was if there was no good reason to code something in C do it in Lua. The C core thus does all the stuff close to the operating system:
 * inotify interface
 * process management, zombie collection
 * signals
 * pipes 
 * logging (also in the core, so it can log itself comfortably)
 * alarms (Lsyncd uses kernel jiffies as alarms, thus it should be year Y2K38 safe (2038 is the year in which an often used time measurement - seconds since 1970 - will overflow.)
Everything else is done in lsyncd.lua.

While Lua is not an object oriented language itself, it supplies a set of features which can emulate its features quite nicely. Shortly spoken, it supports prototyping. Lsyncd makes use of local function scoping to create blocks of code which are loosely connected with each other. A major feature of object oriented language.

A typical "class" in Lsyncd looks like this.

```Lua
MyClass = (function()
    local private = "only visible here"
    local function new() 
        return {foo="bar"}
    end
    -- public interface
    return { new = new }
end)()
```

In plain interpretation this creates a global variable called "MyClass". It defines an unnamed function with internal variables and subfunctions. This functions returns a table of all functions that should be visible from the outside. Then the unnamed function is immediately called on the global definition of "MyClass" and the public interface table stored in "MyClass". `MyClass.new()` is then used to create objects of this class. Or if it is a "Singelton", it has no new function, but other functions, that would be considered "static" in an object oriented language.

Lua has developed a culture for personal adaption - "power patches". To not throw a stick into any package maintainer Lsyncd uses standard from the stock Lua (5.1).

One note about the style of sync{} configs. As you can see in Layer 4 configuration the configuration is selected by merely adding the configuration table as parameter. In the implementation it does nothing else than to take any value with a number as key and which has a table as value and copy all of its key/value entries that are not there already. This even works recursively if that config again imports another config.

Why Lua and not ...?
--------------------
Lua has been chosen for Lsyncd 2 much out of the same reasons why C was chosen for Lsyncd 1.x. Lsyncd should be small and fast and should come with as few as possible dependencies. 

The move from a pure C implementation as in Lsyncd 1.x to using Lua was motivated by enhancing the configuration file format. XML was starting to get clunky for the requirements of Lsyncd. When used Lua bindings already for config file parsing, there was little reason not to move large parts of Lsyncd internal logic into Lua as well. With its garbage collector, memory management became much easier, and with its highly optimized tables as dictionaries, some things even become faster, as Lsyncd 1.x linearly traversed a lot of lists, while Lsyncd 2 uses Lua tables as hash tables everywhere. 

Lua was chosen over other script languages not because I think Lua is the greatest thing on earth - like many coders think about their favorite language, but because it is considered to be a appropriate tool for this task. Many coders like [X] so much, they want to do everything with it, also because after a while they know [X] very well, and are of course faster in solving an issue than learning [Y]. I on the other hand didn't know Lua before Lsyncd 2 at all, and decided explicitly for it out of pragmatic reasons. First it is said have originated out of the need for configuration files for C applications. Much the use case Lsyncd needs. So the C interface is handy and fast. Since Lsyncd has its parts close to the system still coded in C this interfacing makes things easier. Lua is very fast. Without affronting other script languages one can say it is at least in the top tier regarding speed. It may be the fastest scripting language there is, but this statement might raise debates. But not without reason it got quite some attention from the gaming industry. It gets even faster with LuaJIT and it gets into the range of JavaJIT (which is a compiled and not a script language). Lua has also a very slim standard library, while this is a stopper for people just wanting to quickly write some script this plays into the hands of Lsyncd. It comes with little requirements on the machine to be installed and Lsyncd comes with its own custom core functionally anyway. And although the Lua syntax looks a bit dirty at first - like implicit on the fly creation of globals - with a little configuration this can be banned. Like Lsyncd does in normal operation modes. 
