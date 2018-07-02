--
-- delay.lua from Lsyncd -- the Live (Mirror) Syncing Demon
--
--
-- Holds the information about a delayed event for one Sync.
--
-- Valid stati of a delay are:
--   'wait'    ... the event is ready to be handled.
--   'active'  ... there is process running catering for this event.
--   'blocked' ... this event waits for another to be handled first.
--
--
-- License: GPLv2 (see COPYING) or any later version
-- Authors: Axel Kittenberger <axkibe@gmail.com>
--
if lsyncd_version
then
	print( 'Error, Lsyncd mantle already loaded' )
	os.exit( -1 )
end


--
-- Metatable.
--
local mt = { }


--
-- Secret key to native table
--
local k_nt = { }


--
-- On accessing a nil index.
--
mt.__index = function
(
	self,
	k   -- key value accessed
)
	return self[ k_nt ][ k ]
end


--
-- On assigning a new index.
--
mt.__newindex = function
(
	self,
	k,  -- key value to assign to
	v   -- value to assign
)
	error( 'Cannot assign to Delay' )
end


--
-- This delay is being blocked by another delay
--
local function blockedBy
(
	self,  -- this delay
	delay  -- the blocking delay
)
	self[ k_nt ].status = 'block'

	local blocks = delay[ k_nt ].blocks

	if not blocks
	then
		blocks = { }

		delay[ k_nt ].blocks = blocks
	end

	table.insert( blocks, self )
end


--
-- Sets the delay status to 'active'.
--
local function setActive
(
	self
)
	self[ k_nt ].status = 'active'
end

--
-- Sets the delay status to 'wait'
--
local function wait
(
	self,   -- this delay
	alarm   -- alarm for the delay
)
	self[ k_nt ].status = 'wait'

	if alarm ~= nil then self[ k_nt ].alarm = alarm end
end


--
-- Puts this delay as replacement on a queue.
--
local function replaceAt
(
	self,
	queue,
	dpos
)
	queue:replace( dpos, self )
	self[ k_nt ].dpos = dpos
end


--
-- Pushes this delay on a queue and remembers the position.
--
local function pushOn
(
	self,
	queue
)
	self[ k_nt ].dpos = queue:push( nd )
end

--
-- Creates a new delay.
--
local function new
(
	etype,  -- type of event.
	--         'Create', 'Modify', 'Attrib', 'Delete' or 'Move'
	sync,   -- the Sync this delay belongs to
	alarm,  -- latest point in time this should be catered for
	path,   -- path and file-/dirname of the delay relative
	--      -- to the syncs root.
	path2   -- used only in moves, path and file-/dirname of
	        -- move destination
)
	local delay =
	{
		blockedBy = blockedBy,
		setActive = setActive,
		wait = wait,
		replaceAt = replaceAt,
		pushOn = pushOn,
		[ k_nt ] =
			{
				etype = etype,
				sync = sync,
				alarm = alarm,
				path = path,
				path2  = path2,
				status = 'wait'
			},
	}

	setmetatable( delay, mt )

	return delay
end


--
-- Exported interface
--
Delay = { new = new }

