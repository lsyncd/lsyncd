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
-- transforms a signal name or number to
-- a valid number. 'false' is left be 'false'
--
-- In case of a invalid signal specifie an error is raised.
--
function signum
(
	signal
)
	if type( signal ) == 'number'
	then
		if signal < 0
		or signal ~= signal
		or signal - floor( signal ) ~= 0
		then
			error( 'signal ' .. signal .. ' is an invalid number.' , 3 )
		end

		return signal
	elseif type( signal ) == 'string'
	then
		local sn = signums[ signal ]
		if sn == nil then error( 'signal "' .. signal .. '" unknown.' , 3 ) end
		return sn
	elseif signal == false
	then
		return false
	else
		error( 'signal of type ' .. type( signal ) .. ' invalid.', 3 )
	end
end


--
-- The onsignal( ) function exported to userEnv.
--
function onsignal
(
	...
	--- signal1, -- signal number or name
	--- handler1 -- function to call
	--           -- or nil to unload the handle
	--           -- or false to disable default signal handlers
	-- signal2, handler2
	-- signal3, handler3
	-- and so on
)
	local n = select( '#', ... )
	local arg = {...}

	if n % 2 ~= 0
	then
		error( 'onsignal() with uneven number of arguments called', 2 )
	end

	for a = 1, n, 2
	do
		local signal = arg[ a ]
		local handler = arg[ a + 1 ]
		local sn = signum( signal )
		sigHandlers[ sn ] = handler
	end

	core.onsignal( sigHandlers )
end


--
-- Returns signal handler registered for 'signum'
--
function getsignal
(
	signum
)
	return sigHandlers[ signum ];
end


--
-- Called by kernel on catched and queued signals
--
mci.signalEvent =
	function
(
	sigtable
)
	for _, sn in ipairs( sigtable )
	do
		local handler = sigHandlers[ sn ]

		if not handler
		then
			log( 'Error', 'Received signal ',sn,' without a handler.' )
		end

		handler( )
	end
end


--
-- Sends a signal to another process.
--
function signal
(
	pid,     -- process to send the signal to
	signal  -- the signal to send
)
	core.kill( pid, signum( signal ) )
end


