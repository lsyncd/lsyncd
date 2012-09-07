----
-- Lsyncd user-script that creates a "magic" image converter directory.
--
-- This configuration will automatically convert all images that are placed
-- in the directory 'magicdir' all resulting images are placed in the same
-- directory!
--
-- Be sure to mkdir 'magicdir' first.

-----
-- Fileformats:   .jpg  .gif  .png
--
local formats = { jpg=true, gif=true, png=true,  }

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
					-- excludes files to be created, so no
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

	-----
	-- Does not collapse anything
	collapse = function()
		return 3
	end,
}

sync{convert, source="magicdir", subdirs=false}

