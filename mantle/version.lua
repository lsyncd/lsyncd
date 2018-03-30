--
-- version.lua from Lsyncd -- the Live (Mirror) Syncing Demon
--
--
-- Sets the lsyncd_version of the mantle,
-- this must come as last mantle file as other
-- mantle scripts check that global against accidental loading.
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


-- Lets the core double check version identity with the mantle
lsyncd_version = '3.0.0-devel'

