require( 'posix' )
dofile( 'tests/testlib.lua' )

cwriteln( '****************************************************************' )
cwriteln( ' Testing crontab (rsync)' )
cwriteln( '****************************************************************' )

local tdir, srcdir, trgdir = mktemps( )
local logfile = tdir .. "log"
local cfgfile = tdir .. "config.lua"
local range = 5
local log = {"-log", "all"}

writefile(cfgfile, [[
settings {
        logfile = "]]..logfile..[[",
        nodaemon = true,
}

sync {
        default.rsync,
        crontab = {
		-- trigger full sync every 1 minute
                "*/10 * * * * *",
        },
        source = "]]..srcdir..[[",
        action = function (inlet)
                local e = inlet.getEvent( );
                print("inhibit action ".. e.path.. " = " .. e.etype);
                if e.etype ~= "Full" then
                    inlet.discardEvent(e);
                    return;
                end
                return default.rsync.action(inlet);
        end,
        target    = "]]..trgdir..[[",
        delay = 2,
        delete = true,
        rsync = {
                verbose = true,
                inplace = true,
                _extra = {
                        "-vv",
                        "--info=progress2"
                }
        },
        filter = {
            '- /xb**',
            '+ /x**',
            '- /**',
        },
}
]])

-- writes all files
local function writefiles
( )
	writefile( srcdir .. 'xbc', 'xbc' )
	writefile( srcdir .. 'xcc', 'xcc' )
	writefile( srcdir .. 'yaa', 'yaa' )
	posix.mkdir( srcdir .. 'xbx' )
	writefile( srcdir .. 'xbx/a', 'xbxa' )
	posix.mkdir( srcdir .. 'xcx' )
	writefile( srcdir .. 'xcx/x', 'xcxx' )
end

-- test all files
local function testfiles
( )
	testfile( trgdir .. 'xbc', false )
	testfile( trgdir .. 'xcc', true )
	testfile( trgdir .. 'yaa', false )
	testfile( trgdir .. 'xbx/a', false )
	testfile( trgdir .. 'xcx/x', true )
end


cwriteln( 'testing crontab' )

writefiles( )

cwriteln( 'starting Lsyncd' )

local pid = spawn( './lsyncd', cfgfile, '-log', 'all' )

cwriteln( 'waiting for Lsyncd to start' )

posix.sleep( 3 )

cwriteln( 'testing filters after startup' )

testfiles( )

cwriteln( 'ok, removing sources' )

if srcdir:sub( 1,4 ) ~= '/tmp'
then
	-- just to make sure before rm -rf
	cwriteln( 'exit before drama, srcdir is "', srcdir, '"' )

	os.exit( 1 )
end

os.execute( 'rm -rf '..srcdir..'/*' )

cwriteln( 'waiting for Lsyncd to remove destination' )

posix.sleep( 20 )

local result, code = execute( 'diff -urN ' .. srcdir .. ' ' .. trgdir )

if result ~= 'exit' or code ~= 0
then
	cwriteln( 'fail, target directory not empty!' )
    posix.kill( pid )

	os.exit( 1 )
end

cwriteln( 'writing files after startup' )

writefiles( )

cwriteln( 'waiting for Lsyncd to transmit changes' )

posix.sleep( 20 )

testfiles( )

cwriteln( 'killing started Lsyncd' )

posix.kill( pid )
local _, exitmsg, exitcode = posix.wait( pid )

cwriteln( 'Exitcode of Lsyncd = ', exitmsg, ' ', exitcode );

if exitcode == 143
then
	cwriteln( 'OK' )
	os.exit( 0 )
else
	os.exit( 1 )
end

-- TODO remove temp
