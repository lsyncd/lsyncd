---
layout: default
title: "Example: Auto-Image-Magic"
tab: "manual/examples"
---

This [example](..) is a layer 1 script to make a special "magic" directory in which image files will be converted automatically therein.

The full script:

{% highlight lua %}
local formats = { jpg = true, gif = true, png = true }

convert = {
	delay = 0,

	maxProcesses = 99,

	action = function(inlet)
		local event = inlet.getEvent()

		if event.isdir then
			-- ignores events on dirs
			inlet.discardEvent(event)
			return
		end

		-- extract extension and basefilename
		local p    = event.pathname
		local ext  = string.match(p, ".*%.([^.]+)$")
		local base = string.match(p, "(.*)%.[^.]+$")
		if not formats[ext] then
			-- an unknown extenion
			log("Normal", "not doing something on ."..ext)
			inlet.discardEvent(event)
			return
		end

		-- autoconvert on create and modify
		if event.etype == "Create" or event.etype == "Modify" then
			-- builds one bash command
			local cmd = ""
			-- do for all other extensions
			for k, _ in pairs(formats) do
				if k ~= ext then
					-- excludes files to be created, so no
					-- followup actions will occur
					inlet.addExclude(base..'.'..k)
					if cmd ~= ""  then
						cmd = cmd .. " && "
					end
					cmd = cmd.. 
						'/usr/bin/convert "'..
						event.source..p..'" "'..
						event.source..base..'.'..k..
						'" || /bin/true'
				end
			end
			log("Normal", "Converting "..p)
			spawnShell(event, cmd)
			return
		end
		
		-- deletes all formats if you delete one
		if event.etype == "Delete" then
			-- builds one bash command
			local cmd = ""
			-- do for all other extensions
			for k, _ in pairs(formats) do
				if k ~= ext then
					-- excludes files to be deleted, so no
					-- followup actions will occur
					inlet.addExclude(base..'.'..k)
					if cmd ~= ""  then
						cmd = cmd .. " && "
					end
					cmd = cmd..
						'rm "'..event.source..base..'.'..k..
						'" || /bin/true'
				end
			end
			log("Normal", "Deleting all "..p)
			spawnShell(event, cmd)
			return
		end

		-- ignores other events.
		inlet.discardEvent(event)
	end,

	-----
	-- Removes excludes when convertions are finished
	--
	collect = function(event, exitcode)
		local p     = event.pathname
		local ext   = string.match(p, ".*%.([^.]+)$")
		local base  = string.match(p, "(.*)%.[^.]+$")
		local inlet = event.inlet

		if event.etype == "Create" or
		   event.etype == "Modify" or 
		   event.etype == "Delete" 
		then
			for k, _ in pairs(formats) do
				inlet.rmExclude(base..'.'..k)
			end
		end
	end,

}

sync{convert, source="magicdir", recursive=false}
{% endhighlight %}

This creates a local table of all supported file formats. The file formats are used as keys.

{% highlight lua %}
local formats = { jpg=true, gif=true, png=true,  }
{% endhighlight %}

Configures actions to be instant and there is unlimits the amount the conversion to be done at once. Well not unlimited but set the limit pretty high.

{% highlight lua %}
convert = {
	delay = 0,
	maxProcesses = 99,
{% endhighlight %}

This script uses the _layer 1_ inlet interface altough it greps only single events and not lists. It does this instead of _layer 2_ as it needs to do common operations for all kind of events.

{% highlight lua %}
	action = function(inlet)
		local event = inlet.getEvent()
{% endhighlight %}

Ignores directories. As using _layer 1_ it has to explicitly discard events it does not spawn actions for.

{% highlight lua %}
		if event.isdir then
			-- ignores events on dirs
			inlet.discardEvent(event)
			return
		end
{% endhighlight %}

Uses Lua string patterns to extract the file extension from the rest - here called base.

{% highlight lua %}
		-- extract extension and basefilename
		local p    = event.pathname
		local ext  = string.match(p, ".*%.([^.]+)$")
		local base = string.match(p, "(.*)%.[^.]+$")
{% endhighlight %}

Looks the extension up in the formats table. This can be done, since formats are keys in that table. If not an image format it bails out.

{% highlight lua %}
		if not formats[ext] then
			-- an unknown extenion
			log("Normal", "not doing something on ."..ext)
			inlet.discardEvent(event)
			return
		end
{% endhighlight %}


Following actions will done on "Create" and "Modify" events.

{% highlight lua %}
		-- autoconvert on create and modify
		if event.etype == "Create" or event.etype == "Modify" then
{% endhighlight %}

This script builds a bash command using a string. 

{% highlight lua %}
			-- builds one bash command
			local cmd = ""
{% endhighlight %}

It iterates for all image formats and excludes the one which is the source image.

{% highlight lua %}
			-- do for all other extensions
			for k, _ in pairs(formats) do
				if k ~= ext then
{% endhighlight %}

This is a little trick. It creates Exclusions for the converted images. As this images are not placed in a target directory but right next to the source image in the source directory they would otherwise trigger Create actions as well.

{% highlight lua %}
					-- excludes files to be created, so no
					-- followup actions will occur
					inlet.addExclude(base..'.'..k)
{% endhighlight %}

And for every image to be converted adds the calls to the arguments. It uses ```" || /bin/true "``` to let the shell continue if one conversion fails. In that it chains the conversion with '&&' they will be called sequentially.

{% highlight lua %}
					if cmd ~= ""  then
						cmd = cmd .. " && "
					end
					cmd = cmd.. 
						'/usr/bin/convert "'..
						event.source..p..'" "'..
						event.source..base..'.'..k..
						'" || /bin/true'
{% endhighlight %}

And eventually it spawns the shell doing the conversions and is finished.

{% highlight lua %}
				end
			end
			log("Normal", "Converting "..p)
			spawnShell(event, cmd)
			return
		end
{% endhighlight %}

For deletions it does technically something similar, but it deletes all other file formats of the image.

{% highlight lua %}
		-- deletes all formats if you delete one
		if event.etype == "Delete" then
			-- builds one bash command
			local cmd = ""
			-- do for all other extensions
			for k, _ in pairs(formats) do
				if k ~= ext then
					-- excludes files to be deleted, so no
					-- followup actions will occur
					inlet.addExclude(base..'.'..k)
					if cmd ~= ""  then
						cmd = cmd .. " && "
					end
					cmd = cmd..
						'rm "'..event.source..base..'.'..k..
						'" || /bin/true'
				end
			end
			log("Normal", "Deleting all "..p)
			spawnShell(event, cmd)
			return
		end
{% endhighlight %}

and not to forget to nicely discard all other events.

{% highlight lua %}
		-- ignores other events.
		inlet.discardEvent(event)
	end,
{% endhighlight %}

collect is called when the conversions finished. It will remove the temporary excludes again.

{% highlight lua %}
	-----
	-- Removes excludes when convertions are finished
	--
	collect = function(event, exitcode)
		local p     = event.pathname
		local ext   = string.match(p, ".*%.([^.]+)$")
		local base  = string.match(p, "(.*)%.[^.]+$")
		local inlet = event.inlet

		if event.etype == "Create" or
		   event.etype == "Modify" or 
		   event.etype == "Delete" 
		then
			for k, _ in pairs(formats) do
				inlet.rmExclude(base..'.'..k)
			end
		end
	end,
{% endhighlight %}

And finally use the configuration to watch "magicdir". 

{% highlight lua %}
sync{convert, source="magicdir", recursive=false}
{% endhighlight %}
