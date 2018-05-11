--
-- signal.lua from Lsyncd -- the Live (Mirror) Syncing Demon
--
--
-- Handles signal handlers.
--
--
-- License: GPLv2 (see COPYING) or any later version
-- Authors: Axel Kittenberger <axkibe@gmail.com>
--
if mantle
then
	print( 'Error, Lsyncd mantle already loaded' )
	os.exit( -1 )
end

-- "signames" is a a (mantle-)global table from signames.lua created by the
-- build script following code creates a (hash)table for the other direction.
local signums = { }

for num, name in pairs( signames )
do
	signums[ name ] = num
end


-- a table of all registered signal handlers
--
-- keys are signal numbers
-- values are functions to be called
-- or 'false' in case of disabled default signals
local sigHandlers = { }

-- counter of signal handlers
-- used to tell the core to enlarge the signal queue if needed
-- (the queue must be as large as the number of different signals listened for)
local sigHandlerCount = 0


--
-- Prepares an onsignal handle.
-- It changes the mantle data, but does not yet tell the core about it.
--
-- To be used only internally to combine multiple changes into one.
--
local function onsignalPrep
(
	signal, -- signal number or name
	handler -- function to call
	--      -- or nil to unload the handle
	--      -- or false to disable default signal handlers
)
	local signum

	if type( signal ) == 'number'
	then
		if signal < 0
		or signal ~= signal
		or signal - floor( signal ) ~= 0
		then
			error( 'signal ' .. signal .. ' is an invalid number.' , 2 )
		end
		signum = signal
	elseif type( signal ) == 'string'
	then
		signum = signums[ signal ]
		if signum == nil
		then
			error( 'signal "' .. signal .. '" unknown.' , 2 )
		end
	else
		error( 'signal of type ' .. type( signal ) .. ' invalid.', 2 )
	end

	sigHandlers[ signum ] = handler
end

--
-- The onsignal( ) function exported to userEnv.
--
function onsignal
(
	signal, -- signal number or name
	handler -- function to call
	--      -- or nil to unload the handle
	--      -- or false to disable default signal handlers
)
	onsignalPrep( signal, handler )

	core.onsignal( sigHandlers )
end


--
-- Sets up the default HUP/INT/TERM signal handlers.
--
-- Called after user scripts finished
--
function initSignalHandlers
(
	firstTime --- TODO check if needed
)
	onsignalPrep(
		'HUP',
		function( )
			print( 'GOT A HUP SIGNAL' )
		end
	)

	onsignalPrep(
		'INT',
		function( )
			print( 'GOT A INT SIGNAL' )
		end
	)

	onsignalPrep(
		'TERM',
		function( )
			print( 'GOT A TERM SIGNAL' )
		end
	)

	core.onsignal( sigHandlers )
end


--
-- Called by kernel on catched and queued signals
--
mci.signalEvent =
	function
(
	sigtable
)
	for _, signum in ipairs( sigtable )
	do
		local handler = sigHandlers[ signum ]

		if not handler
		then
			log( 'Error', 'Received signal '..signnum..' without a handler.' )
		end

		handler( )
	end
end

