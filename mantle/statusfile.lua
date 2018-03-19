--~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
-- lsyncd.lua   Live (Mirror) Syncing Demon
--
--
-- Writes a status report file at most every 'statusintervall' seconds.
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


--
-- Timestamp when the status file has been written.
--
local lastWritten = false

--
-- Timestamp when a status file should be written.
--
local alarm = false

--
-- Returns the alarm when the status file should be written-
--
local function getAlarm
( )
	return alarm
end

--
-- Called to check if to write a status file.
--
local function write
(
	timestamp
)
	log( 'Function', 'write( ', timestamp, ' )' )

	--
	-- takes care not to write too often
	--
	if uSettings.statusInterval > 0
	then
		-- already waiting?
		if alarm and timestamp < alarm
		then
			log( 'Statusfile', 'waiting(', timestamp, ' < ', alarm, ')' )

			return
		end

		-- determines when a next write will be possible
		if not alarm
		then
			local nextWrite = lastWritten and timestamp + uSettings.statusInterval

			if nextWrite and timestamp < nextWrite
			then
				log( 'Statusfile', 'setting alarm: ', nextWrite )
				alarm = nextWrite

				return
			end
		end

		lastWritten = timestamp
		alarm = false
	end

	log( 'Statusfile', 'writing now' )

	local f, err = io.open( uSettings.statusFile, 'w' )

	if not f
	then
		log(
			'Error',
			'Cannot open status file "' ..
				uSettings.statusFile ..
				'" :' ..
				err
		)
		return
	end

	f:write( 'Lsyncd status report at ', os.date( ), '\n\n' )

	for i, s in SyncMaster.iwalk( )
	do
		s:statusReport( f )

		f:write( '\n' )
	end

	Inotify.statusReport( f )

	f:close( )
end

--
-- Exported interface.
--
StatusFile = { write = write, getAlarm = getAlarm }

