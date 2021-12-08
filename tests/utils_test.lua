dofile( 'tests/testlib.lua' )

cwriteln( '****************************************************************' )
cwriteln( ' Testing Utils Functions                                         ' )
cwriteln( '****************************************************************' )

assert(isTableEqual(
    splitQuotedString("-p 22 -i '/home/test/bla blu/id_rsa'"),
    {"-p", "22", "-i", "/home/test/bla blu/id_rsa"}
))

os.exit(0)