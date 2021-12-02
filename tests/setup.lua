-- a heavy duty test.
-- makes thousends of random changes to the source tree

require( 'posix' )

dofile( 'tests/testlib.lua' )
cwriteln( ' Start Testsuite '               )

startSshd()
