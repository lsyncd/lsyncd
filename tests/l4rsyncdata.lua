#!/usr/bin/lua
require( 'posix' )
dofile( 'tests/testlib.lua' )

cwriteln( '****************************************************************' )
cwriteln( ' Testing layer 4 default rsync with simulated data activity     ' )
cwriteln( '****************************************************************' )

local tdir, srcdir, trgdir = mktemps()
local logfile = tdir .. 'log'
local range = 5
local log = { '-log', 'all' }

posix.mkdir( srcdir .. 'd'   )
posix.mkdir( srcdir .. 'd/e' )

if not writefile( srcdir .. "d/e/f1", 'test' )
then
	os.exit( 1 )
end
cwriteln( 'starting Lsyncd' )

logs = { }
local pid =
	spawn(
		'./lsyncd',
		'-logfile', logfile,
		'-nodaemon',
		'-delay', '5',
		"-rsync", srcdir, trgdir,
		unpack( logs )
	)

cwriteln( 'waiting for lsyncd to start' )
posix.sleep( 2 )

cwriteln( '* making some data' )
cwriteln( '* creating d[x]/e/f2' )

for i = 1, range
do
	cwriteln( '[cp -r ' .. srcdir .. 'd ' .. srcdir .. 'd' .. i .. ']' )
	os.execute( 'cp -r ' .. srcdir .. 'd ' .. srcdir .. 'd' .. i )
end

-- mkdir -p "$S"/m/n
-- echo 'test3' > "$S"/m/n/file
-- for i in $RANGE; do
--    cp -r "$S"/m "$S"/m$i
--    echo 'test4' > "$S"/m${i}/n/another
-- done

cwriteln( '* waiting for Lsyncd to do its job.' )
posix.sleep( 10 )

cwriteln( '* killing Lsyncd' )

posix.kill( pid )
local _, exitmsg, lexitcode = posix.wait(lpid)
cwriteln( 'Exitcode of Lsyncd = ', exitmsg, ' ', lexitcode)
posix.sleep( 1 )

cwriteln( '* differences:' )
_, result, code = os.execute( 'diff -urN ' .. srcdir .. ' ' .. trgdir )

if result == 'exit'
then
	cwriteln( 'Exitcode of diff = "', code, '"')
else
	cwriteln( 'Signal terminating diff = "', code, '"')
end

if result ~= 'exit' or code ~= 0
then
	os.exit( 1 )
else
	os.exit( 0 )
end

-- TODO remove temp
