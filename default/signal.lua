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


--
--
--
local function onCollect
(
	sync -- the user intf to the sync a child finished for
)
	if( sync.processCount( ) == 0 ) then syncs.remove( sync ) end

	if #syncs == 0 then os.exit( 0 ) end
end


local function sighup
( )
	print( 'GOT A HUP SIGNAL' )

	os.exit( 1 )
end


local function sigint
( )
	log( 'Normal', 'Received an INT signal, terminating' )

	local pCount = 0

	for _, sync in ipairs( syncs )
	do
		sync.stop( )

		local c = sync.processCount( )

		if( c == 0 )
		then
			syncs.remove( sync )
		else
			pCount = pCount + c
			sync.onCollect( onCollect )
		end
	end

	if #syncs == 0 then os.exit( 0 ) end

	log( 'Normal', 'Waiting for ', pCount, ' child processes.' )
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
		'TERM', term
	)
end

