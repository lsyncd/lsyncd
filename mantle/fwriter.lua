--
-- fwrite.lua from Lsyncd -- the Live (Mirror) Syncing Demon
--
--
-- Writes functions for the user for layer 3 configurations.
--
--
-- This code assumes your editor is at least 100 chars wide.
--
-- License: GPLv2 (see COPYING) or any later version
-- Authors: Axel Kittenberger <axkibe@gmail.com>
--
if mantle
then
	print( 'Error, Lsyncd mantle already loaded' )
	os.exit( -1 )
end


--
-- All variables known to layer 3 configs.
--
transVars = {
	{ '%^pathname',          'event.pathname',        1 },
	{ '%^pathdir',           'event.pathdir',         1 },
	{ '%^path',              'event.path',            1 },
	{ '%^sourcePathname',    'event.sourcePathname',  1 },
	{ '%^sourcePathdir',     'event.sourcePathdir',   1 },
	{ '%^sourcePath',        'event.sourcePath',      1 },
	{ '%^source',            'event.source',          1 },
	{ '%^targetPathname',    'event.targetPathname',  1 },
	{ '%^targetPathdir',     'event.targetPathdir',   1 },
	{ '%^targetPath',        'event.targetPath',      1 },
	{ '%^target',            'event.target',          1 },
	{ '%^o%.pathname',       'event.pathname',        1 },
	{ '%^o%.path',           'event.path',            1 },
	{ '%^o%.sourcePathname', 'event.sourcePathname',  1 },
	{ '%^o%.sourcePathdir',  'event.sourcePathdir',   1 },
	{ '%^o%.sourcePath',     'event.sourcePath',      1 },
	{ '%^o%.targetPathname', 'event.targetPathname',  1 },
	{ '%^o%.targetPathdir',  'event.targetPathdir',   1 },
	{ '%^o%.targetPath',     'event.targetPath',      1 },
	{ '%^d%.pathname',       'event2.pathname',       2 },
	{ '%^d%.path',           'event2.path',           2 },
	{ '%^d%.sourcePathname', 'event2.sourcePathname', 2 },
	{ '%^d%.sourcePathdir',  'event2.sourcePathdir',  2 },
	{ '%^d%.sourcePath',     'event2.sourcePath',     2 },
	{ '%^d%.targetPathname', 'event2.targetPathname', 2 },
	{ '%^d%.targetPathdir',  'event2.targetPathdir',  2 },
	{ '%^d%.targetPath',     'event2.targetPath',     2 },
}

--
-- Splits a user string into its arguments.
-- Returns a table of arguments
--
local function splitStr(
	str -- a string where parameters are seperated by spaces.
)
	local args = { }

	while str ~= ''
	do
		-- break where argument stops
		local bp = #str

		-- in a quote
		local inQuote = false

		-- tests characters to be space and not within quotes
		for i = 1, #str
		do
			local c = string.sub( str, i, i )

			if c == '"'
			then
				inQuote = not inQuote
			elseif c == ' ' and not inQuote
			then
				bp = i - 1

				break
			end
		end

		local arg = string.sub( str, 1, bp )
		arg = string.gsub( arg, '"', '\\"' )
		table.insert( args, arg )
		str = string.sub( str, bp + 1, -1 )
		str = string.match( str, '^%s*(.-)%s*$' )

	end

	return args
end


--
-- Translates a call to a binary to a lua function.
-- TODO this has a little too blocking.
--
local function translateBinary
(
	str
)
	-- splits the string
	local args = splitStr( str )

	-- true if there is a second event
	local haveEvent2 = false

	for ia, iv in ipairs( args )
	do
		-- a list of arguments this arg is being split into
		local a = { { true, iv } }

		-- goes through all translates
		for _, v in ipairs( transVars )
		do
			local ai = 1
			while ai <= #a
			do
				if a[ ai ][ 1 ]
				then
					local pre, post =
						string.match( a[ ai ][ 2 ], '(.*)'..v[1]..'(.*)' )

					if pre
					then
						if v[3] > 1
						then
							haveEvent2 = true
						end

						if pre ~= ''
						then
							table.insert( a, ai, { true, pre } )
							ai = ai + 1
						end

						a[ ai ] = { false, v[ 2 ] }

						if post ~= ''
						then
							table.insert( a, ai + 1, { true, post } )
						end
					end
				end
				ai = ai + 1
			end
		end

		-- concats the argument pieces into a string.
		local as = ''
		local first = true

		for _, v in ipairs( a )
		do
			if not first then as = as..' .. ' end

			if v[ 1 ]
			then
				as = as .. '"' .. v[ 2 ] .. '"'
			else
				as = as .. v[ 2 ]
			end

			first = false
		end

		args[ ia ] = as
	end

	local ft

	if not haveEvent2
	then
		ft = 'function( event )\n'
	else
		ft = 'function( event, event2 )\n'
	end

	ft = ft ..
		"    log('Normal', 'Event ', event.etype, \n" ..
		"        ' spawns action \"".. str.."\"')\n" ..
		"    spawn( event"

	for _, v in ipairs( args )
	do
		ft = ft .. ',\n         ' .. v
	end

	ft = ft .. ')\nend'
	return ft

end


--
-- Translates a call using a shell to a lua function
--
local function translateShell
(
	str
)
	local argn = 1

	local args = { }

	local cmd = str

	local lc = str

	-- true if there is a second event
	local haveEvent2 = false

	for _, v in ipairs( transVars )
	do
		local occur = false

		cmd = string.gsub(
			cmd,
			v[ 1 ],
			function
			( )
				occur = true
				return '"$' .. argn .. '"'
			end
		)

		lc = string.gsub( lc, v[1], ']]..' .. v[2] .. '..[[' )

		if occur
		then
			argn = argn + 1

			table.insert( args, v[ 2 ] )

			if v[ 3 ] > 1
			then
				haveEvent2 = true
			end
		end

	end

	local ft

	if not haveEvent2
	then
		ft = 'function( event )\n'
	else
		ft = 'function( event, event2 )\n'
	end

	-- TODO do array joining instead
	ft = ft..
		"    log('Normal', 'Event ',event.etype,\n"..
		"        [[ spawns shell \""..lc.."\"]])\n"..
		"    spawnShell(event, [["..cmd.."]]"

	for _, v in ipairs( args )
	do
		ft = ft..',\n         '..v
	end

	ft = ft .. ')\nend'

	return ft

end

--
-- Writes a lua function for a layer 3 user script.
--
local function translate
(
	str
)
	-- trims spaces
	str = string.match( str, '^%s*(.-)%s*$' )

	local ft

	if string.byte( str, 1, 1 ) == 47
	then
		-- starts with /
		 ft = translateBinary( str )
	elseif string.byte( str, 1, 1 ) == 94
	then
		-- starts with ^
		 ft = translateShell( str:sub( 2, -1 ) )
	else
		 ft = translateShell( str )
	end

	log( 'FWrite', 'translated "', str, '" to \n', ft )

	return ft
end


--
-- Exporter interface.
--
FWriter = { translate = translate }

