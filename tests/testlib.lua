-- common testing environment
posix = require( 'posix' )

-- escape codes to colorize output on terminal
local c1='\027[47;34m'

local c0='\027[0m'

--
-- Writes colorized.
--
function cwriteln
(...)
	io.write( c1, '++ ', ... )
	io.write( c0, '\n' )
end

--
-- Initializes the pseudo random generator
--
-- If environment variable 'SEED' is set,
-- that one is used seed.
--
local seed = os.getenv( 'SEED')  or os.time( )

math.randomseed( seed )

cwriteln( 'random seed: ', seed )

--
-- Creates a tmp directory.
--
-- Returns the name of the directory.
--
function mktempd
( )
	local f = io.popen( 'mktemp -td ltest.XXX', 'r' )

	local s = f:read( '*a' )

	f:close( )

	s = s:gsub( '[\n\r]+', ' ' )

	s = s:match( '^%s*(.-)%s*$' )

	return s
end

--
-- Creates a tmp directory with the
-- typical lsyncd test architecture.
--
-- returns path of tmpdir
--         path of srcdir
--         path of trgdir
--
function mktemps
( )
	local tdir = mktempd() .. '/'

	cwriteln( 'using ', tdir, ' as test root' )

	local srcdir = tdir..'src/'

	local trgdir = tdir..'trg/'

	posix.mkdir( srcdir )

	posix.mkdir( trgdir )

	return tdir, srcdir, trgdir
end

--
-- Writes a file with 'text' in it
-- and adds a newline.
--
function writefile
(
	filename,
	text
)
	local f = io.open( filename, 'w' )

	if not f
	then
		cwriteln( 'Cannot open "'..filename..'" for writing.' )
		return false
	end

	f:write( text )

	f:write( '\n' )

	f:close( )

	return true
end

--
-- Spawns a subprocess.
--
-- Returns the processes pid.
--
function spawn
(...)
	args = { ... }

	cwriteln( 'spawning: ', table.concat( args, ' ' ) )

	local pid = posix.fork( )

	if pid < 0
	then
		cwriteln( 'Error, failed fork!' )

		os.exit( -1 )
	end

	if pid == 0
	then
		posix.exec( ... )
		-- should not return

		cwriteln( 'Error, failed to spawn: ', ... )

		os.exit( -1 )
	end

	return pid
end

--
-- Makes a lot of random data
--
--
function churn
(
	rootdir,  -- the directory to make data in
	n,        -- roughly how much data action will be done
	init      -- if true init random data only, no sleeps or moves
)
	-- all dirs created, indexed by integer and path
	root = { name = '' }

	alldirs = { root }

	dirsWithFileI = { }

	dirsWithFileD = { }

	--
	-- returns the name of a directory
	--
	-- name is internal recursive paramter, keep it nil.
	--
	local function dirname
	(
		dir,
		name
	)
		name = name or ''

		if not dir
		then
			return name
		end

		return dirname( dir.parent, dir.name .. '/' .. name )
	end

	--
	-- Picks a random dir.
	--
	local function pickDir
	(
		notRoot
	)
		if notRoot
		then
			if #alldirs <= 2
			then
				return nil
			end

			return alldirs[ math.random( 2, #alldirs ) ]
		end

		return alldirs[ math.random( #alldirs ) ]
	end

	--
	-- Picks a random file.
	--
	-- Returns 3 values:
	--  * the directory
	--  * the filename
	--  * number of files in directory
	--
	local function pickFile
	( )
		-- picks the random directory
		if #dirsWithFileI < 1
		then
			return
		end

		local rdir = dirsWithFileI[ math.random( 1, #dirsWithFileI ) ]

		if not rdir
		then
			return
		end

		-- counts the files in there
		local c = 0

		for name, _ in pairs(rdir)
		do
			if #name == 2
			then
				c = c + 1
			end
		end

		-- picks one file at random
		local cr = math.random( 1, c )

		local fn

		for name, _ in pairs( rdir )
		do
			if #name == 2
			then
				-- filenames are 2 chars wide.
				cr = cr - 1
				if cr == 0
				then
					fn = name
					break
				end
			end
		end

		return rdir, fn, c
	end

	--
	-- Removes a reference to a file
	--
	-- @param dir  -- directory reference
	-- @param fn   -- filename
	-- @param c    -- number of files in dir
	--
	local function rmFileReference
	( dir, fn, c )
		dir[fn] = nil

		if c == 1
		then
			-- if last file from origin dir, it has no files anymore
			for i, v in ipairs( dirsWithFileI )
			do
				if v == dir
				then
					table.remove( dirsWithFileI, i )

					break
				end
			end

			dirsWithFileD[ dir ] = nil
		end
	end

	--
	-- possible randomized behaviour.
	-- just gives it a pause
	--
	local function sleep
	( )
		cwriteln( '..zzz..' )

		posix.sleep( 1 )
	end

	--
	-- possible randomized behaviour.
	-- creates a directory
	--
	local function mkdir
	( )
		-- chooses a random directory to create it into
		local rdir = pickDir( )

		-- creates a new random one letter name
		local nn = string.char( 96 + math.random( 26 ) )

		if not rdir[nn]
		then
			local ndir = {
				name   = nn,
				parent = rdir,
			}

			local dn = dirname( ndir )

			rdir[ nn ] = dn

			table.insert( alldirs, ndir )

			cwriteln( 'mkdir  '..rootdir..dn )

			posix.mkdir( rootdir..dn )
		end
	end

	--
	-- Possible randomized behaviour:
	-- Creates a file.
	--
	local function mkfile
	( )
		-- chooses a random directory to create it into
		local rdir = pickDir()

		-- creates a new random one letter name
		local nn = 'f'..string.char( 96 + math.random( 26 ) )

		local fn = dirname( rdir ) .. nn

		cwriteln( 'mkfile ' .. rootdir .. fn )

		local f = io.open(rootdir..fn, 'w')

		if f
		then
			for i = 1, 10
			do
				f:write( string.char( 96 + math.random( 26 ) ) )
			end

			f:write( '\n' )

			f:close( )

			rdir[ nn ]=true

			if not dirsWithFileD[ rdir ]
			then
				table.insert( dirsWithFileI, rdir )

				dirsWithFileD[ rdir ]=true
			end
		end
	end

	--
	-- Possible randomized behaviour:
	-- Moves a directory.
	--
	local function mvdir
	( )
		if #alldirs <= 2
		then
			return
		end

		-- chooses a random directory to move
		local odir = pickDir( true )

		-- chooses a random directory to move to
		local tdir = pickDir( )

		-- makes sure tdir is not a subdir of odir
		local dd = tdir

		while dd
		do
			if odir == dd
			then
				return
			end

			dd = dd.parent
		end

		-- origin name in the target dir already
		if tdir[odir.name] ~= nil
		then
			return
		end

		local on = dirname( odir )

		local tn = dirname( tdir )

		cwriteln( 'mvdir  ', rootdir,on, ' -> ', rootdir, tn, odir.name )

		os.rename( rootdir..on, rootdir..tn..odir.name )

		odir.parent[ odir.name ] = nil

		odir.parent = tdir

		tdir[ odir.name ] = odir
	end

	--
	-- possible randomized behaviour,
	-- moves a file.
	--
	local function mvfile
	( )
		local odir, fn, c = pickFile( )

		if not odir
		then
			return
		end

		-- picks a directory with a file at random
		-- picks a target directory at random
		local tdir = pickDir( )

		local on = dirname( odir )

		local tn = dirname( tdir )

		cwriteln( 'mvfile ', rootdir, on, fn, ' -> ', rootdir, tn, fn )

		os.rename( rootdir..on..fn, rootdir..tn..fn )
		rmFileReference( odir, fn, c )

		tdir[ fn ] = true

		if not dirsWithFileD[ tdir ]
		then
			dirsWithFileD[ tdir ] = true

			table.insert( dirsWithFileI, tdir )
		end
	end

	--
	-- Possible randomized behaviour:
	-- Removes a file.
	--
	local function rmfile
	( )
		local dir, fn, c = pickFile( )

		if dir
		then
			local dn = dirname( dir )

			cwriteln( 'rmfile ', rootdir, dn, fn )

			posix.unlink( rootdir..dn..fn )

			rmFileReference( dir, fn, c )
		end
	end

	local dice

	if init
	then
		dice =
		{
			{ 10,   mkfile },
			{ 10, 	mkdir  },
		}
	else
		dice =
		{
			{ 50,	sleep  },
			{ 20,   mkfile },
			{ 20, 	mkdir  },
			{ 20,   mvdir  },
			{ 20,   rmfile },
		}
	end

	cwriteln( 'making random data' )

	local ndice = 0

	for i, d in ipairs( dice )
	do
		ndice = ndice + d[ 1 ]
		d[ 1 ] = ndice
	end

	for ai = 1, n
	do
		-- throws a die what to do
		local acn = math.random( ndice )

		for i, d in ipairs( dice )
		do
			if acn <= d[ 1 ]
			then
				d[ 2 ]( )

				break
			end
		end
	end
end


