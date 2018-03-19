--~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
-- lsyncd.lua   Live (Mirror) Syncing Demon
--
--
-- String comfort routines, also exported to user scripts.
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
-- Comfort routines, also for user.
-- Returns true if 'str' starts with 'start'
--
function string.starts
(
	str,
	start
)
	return string.sub( str, 1, #start ) == start
end


--
-- Comfort routine, also for user.
-- Returns true if 'str' ends with 'ends'
--
function string.ends
(
	str,
	ends
)
	return ends == '' or string.sub( str, -#ends ) == ends
end

