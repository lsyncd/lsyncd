--
-- signal.lua from Lsyncd -- the Live (Mirror) Syncing Demon
--
--
-- The default signal handles for HUP, INT and TERM.
--
--
-- License: GPLv2 (see COPYING) or any later version
-- Authors: Axel Kittenberger <axkibe@gmail.com>
--
if not default then error( 'default not loaded' ) end


default.signal = { }


local function sighup
( )
	print( 'GOT A HUP SIGNAL' )

	os.exit( 1 )
end


local function sigint
( )
	print( 'GOT AN INT SIGNAL' )

	os.exit( 1 )
end


local function sigterm
( )
	print( 'GOT A TERM SIGNAL' )

	os.exit( 1 )
end


--
-- Sets up the default HUP/INT/TERM signal handlers.
--
-- Called after user scripts finished
--
init =
	function
( )
	local hup = getsignal( 'HUP' )
	local int = getsignal( 'INT' )
	local term = getsignal( 'TERM' )

	if hup ~= false then hup = sighup end
	if int ~= false then int = sigint end
	if term ~= false then term = sigterm end

	onsignal(
		'HUP', hup,
		'INT', int,
		'TERM', iterm
	)
end
