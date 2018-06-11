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
-- Returns a signal handler for 'signal'.
--
local function makeSignalHandler
(
	sig,      -- the signal to handle
	forward,  -- the signal to forward to children
	--        -- if nil it doesn't
	logtext,  -- text to log
	finish    -- function to call after all child processes have been collected
)
	return function( )
		log( 'Normal', 'Received an ',sig,' signal, ',logtext )

		local pCount = 0

		for _, sync in ipairs( syncs )
		do
			sync.stop( )

			local pids = sync.pids( )
			local pc = #pids

			if( pc == 0 )
			then
				syncs.remove( sync )
			else
				pCount = pCount + pc

				sync.onCollect(
					function
					(
						sync -- the user intf to the sync a child finished for
					)
						if( sync.processCount( ) == 0 ) then syncs.remove( sync ) end

						if #syncs == 0 then finish( ) end
					end
				)

				if forward
				then
					for _, pid in ipairs( pids ) do signal( pid, forward ) end
				end
			end
		end

		if #syncs == 0
		then
			finish( )
		else
			log( 'Normal', 'Waiting for ', pCount, ' child processes.' )
		end
	end
end


local function finishHup( )
	softreset( )
end

local function finishInt( )
	os.exit( 0 )
end

local function finishTerm( )
	os.exit( 0 )
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
	local usr1 = getsignal( 'USR1' )

	if hup ~= false
	then
		hup = makeSignalHandler( 'HUP', nil, 'resetting', finishHup )
	end

	if int ~= false
	then
		int = makeSignalHandler( 'INT', 'INT', 'terminating', finishInt )
	end

	if term ~= false
	then
		term = makeSignalHandler( 'TERM', 'TERM', 'terminating', finishTerm )
	end

	if usr1 ~= false
	then
		usr1 = makeSignalHandler( 'USR1', nil, 'terminating', finishTerm )
	end

	onsignal( 'HUP', hup, 'INT', int, 'TERM', term, 'USR1', usr1 )
end

