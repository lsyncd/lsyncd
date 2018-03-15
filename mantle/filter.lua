--~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
-- filter.lua
--
--
-- A set of filter patterns.
--
-- Filters allow excludes and includes
--
--
--
-- This code assumes your editor is at least 100 chars wide.
--
-- License: GPLv2 (see COPYING) or any later version
-- Authors: Axel Kittenberger <axkibe@gmail.com>
--
--~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~


if lsyncd_version
then
	print( 'Error, Lsyncd mantle already loaded' )
	os.exit( -1 )
end


--
-- Turns a rsync like file pattern to a lua pattern.
-- ( at best it can )
--
local function toLuaPattern
(
	p  --  the rsync like pattern
)
	local o = p

	p = string.gsub( p, '%%', '%%%%'  )
	p = string.gsub( p, '%^', '%%^'   )
	p = string.gsub( p, '%$', '%%$'   )
	p = string.gsub( p, '%(', '%%('   )
	p = string.gsub( p, '%)', '%%)'   )
	p = string.gsub( p, '%.', '%%.'   )
	p = string.gsub( p, '%[', '%%['   )
	p = string.gsub( p, '%]', '%%]'   )
	p = string.gsub( p, '%+', '%%+'   )
	p = string.gsub( p, '%-', '%%-'   )
	p = string.gsub( p, '%?', '[^/]'  )
	p = string.gsub( p, '%*', '[^/]*' )
	-- this was a ** before
	p = string.gsub( p, '%[%^/%]%*%[%^/%]%*', '.*' )
	p = string.gsub( p, '^/', '^/'    )

	if p:sub( 1, 2 ) ~= '^/'
	then
		-- if does not begin with '^/'
		-- then all matches should begin with '/'.
		p = '/' .. p;
	end

	log( 'Filter', 'toLuaPattern "', o, '" = "', p, '"' )

	return p
end

--
-- Appends a filter pattern
--
local function append
(
	self,    -- the filters object
	line     -- filter line
)
	local rule, pattern = string.match( line, '%s*([+|-])%s*(.*)' )

	if not rule or not pattern
	then
		log( 'Error', 'Unknown filter rule: "', line, '"' )
		terminate( -1 )
	end

	local lp = toLuaPattern( pattern )

	table.insert( self. list, { rule = rule, pattern = pattern, lp = lp } )
end

--
-- Adds a list of patterns to filter.
--
local function appendList
(
	self,
	plist
)
	for _, v in ipairs( plist )
	do
		append( self, v )
	end
end

--
-- Loads the filters from a file.
--
local function loadFile
(
	self,  -- self
	file   -- filename to load from
)
	f, err = io.open( file )

	if not f
	then
		log( 'Error', 'Cannot open filter file "', file, '": ', err )

		terminate( -1 )
	end

	for line in f:lines( )
	do
		if string.match( line, '^%s*#' )
		or string.match( line, '^%s*$' )
		then
			-- a comment or empty line: ignore
		else
			append( self, line )
		end
	end

	f:close( )
end

--
-- Tests if 'path' is filtered.
-- Returns false if it is to be filtered.
--
local function test
(
	self,  -- self
	path   -- the path to test
)
	if path:byte( 1 ) ~= 47
	then
		error( 'Paths for filter tests must start with \'/\'' )
	end

	for _, entry in ipairs( self.list )
	do
		local rule = entry.rule
		local lp = entry.lp -- lua pattern

		if lp:byte( -1 ) == 36
		then
			-- ends with $
			if path:match( lp )
			then
				return rule == '+'
			end
		else
			-- ends either end with / or $
			if path:match( lp .. '/' )
			or path:match( lp .. '$' )
			then
				return rule == '+'
			end
		end
	end

	return true
end

--
-- Cretes a new filter set.
--
local function new
( )
	return {
		list = { },
		-- functions
		append     = append,
		appendList = appendList,
		loadFile   = loadFile,
		test       = test,
	}
end


--
-- Exported interface.
--
Filter = { new = new }

