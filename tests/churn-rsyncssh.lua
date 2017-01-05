#!/usr/bin/lua
-- a heavy duty test.
-- makes thousends of random changes to the source tree

require( 'posix' )

dofile( 'tests/testlib.lua' )

cwriteln( '****************************************************************' )
cwriteln( ' Testing default.rsyncssh with random data activity             ' )
cwriteln( '****************************************************************' )
cwriteln( '( this test needs passwordless ssh localhost access            )' )
cwriteln( '( for current user                                             )' )

local tdir, srcdir, trgdir = mktemps()

-- makes some startup data
churn( srcdir, 5, true )

local logs = {}
logs =  { '-log', 'Delay' }

local pid = spawn(
	'./lsyncd',
	'-nodaemon',
	'-delay',
	'5',
	'-rsyncssh',
	srcdir,
	'localhost',
	trgdir,
	unpack(logs)
)

cwriteln( 'waiting for Lsyncd to startup' )
posix.sleep( 1 )

churn( srcdir, 150, false )

cwriteln( 'waiting for Lsyncd to finish its jobs.' )
posix.sleep( 10 )

cwriteln( 'killing the Lsyncd daemon' )

posix.kill(pid)

local _, exitmsg, lexitcode = posix.wait( lpid )

cwriteln( 'Exitcode of Lsyncd = ', exitmsg, ' ', lexitcode )

_, result, code = os.execute( 'diff -r ' .. srcdir .. ' ' .. trgdir )

if result == 'exit'
then
	cwriteln( 'Exitcode of diff = ', code  )
else
	cwriteln( 'Signal terminating diff = ', code )
end

if code ~= 0
then
	os.exit( 1 )
else
	os.exit( 0 )
end

