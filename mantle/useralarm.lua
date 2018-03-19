--~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
-- lsyncd.lua   Live (Mirror) Syncing Demon
--
--
-- Lets userscripts make their own alarms.
--
--
-- This code assumes your editor is at least 100 chars wide.
--
-- License: GPLv2 (see COPYING) or any later version
-- Authors: Axel Kittenberger <axkibe@gmail.com>
--
--~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~


if mantle
then
	print( 'Error, Lsyncd mantle already loaded' )
	os.exit( -1 )
end


local alarms = { }

--
-- Calls the user function at timestamp.
--
local function alarm
(
	timestamp,
	func,
	extra
)
	local idx

	for k, v in ipairs( alarms )
	do
		if timestamp < v.timestamp
		then
			idx = k

			break
		end
	end

	local a =
	{
		timestamp = timestamp,
		func = func,
		extra = extra
	}

	if idx
	then
		table.insert( alarms, idx, a )
	else
		table.insert( alarms, a )
	end
end


--
-- Retrieves the soonest alarm.
--
local function getAlarm
( )
	if #alarms == 0
	then
		return false
	else
		return alarms[1].timestamp
	end
end


--
-- Calls user alarms.
--
local function invoke
(
	timestamp
)
	while #alarms > 0
	and alarms[ 1 ].timestamp <= timestamp
	do
		alarms[ 1 ].func( alarms[ 1 ].timestamp, alarms[ 1 ].extra )
		table.remove( alarms, 1 )
	end
end


--
-- Exported interface.
--
UserAlarms =
{
	alarm    = alarm,
	getAlarm = getAlarm,
	invoke   = invoke
}

