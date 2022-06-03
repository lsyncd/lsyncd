dofile( 'tests/testlib.lua' )

cwriteln( '****************************************************************' )
cwriteln( ' Testing Utils Functions                                         ' )
cwriteln( '****************************************************************' )

assert(isTableEqual(
    lsyncd.splitQuotedString("-p 22 -i '/home/test/bla blu/id_rsa'"),
    {"-p", "22", "-i", "/home/test/bla blu/id_rsa"}
))

-- test string replacement
local testData = {
    localPort = 1234,
    localHost = "localhorst"
}

assert(isTableEqual(
    substitudeCommands({"-p^doesNotExist", "2^localHostA", "-i '^localPort'"}, testData),
    {"-p^doesNotExist", "2localhorstA", "-i '1234'"}
))

assert(
    substitudeCommands("-p^doesNotExist 2^localHostA -i '^localPort'", testData),
    "-p^doesNotExist 2localhorstA -i '1234'"
)


assert(type(lsyncd.get_free_port()) == "number")

os.exit(0)