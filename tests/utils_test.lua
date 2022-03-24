dofile( 'tests/testlib.lua' )

cwriteln( '****************************************************************' )
cwriteln( ' Testing Utils Functions                                         ' )
cwriteln( '****************************************************************' )

assert(isTableEqual(
    splitQuotedString("-p 22 -i '/home/test/bla blu/id_rsa'"),
    {"-p", "22", "-i", "/home/test/bla blu/id_rsa"}
))

-- test string replacement
local testData = {
    localPort = 1234,
    localHost = "localhorst"
}
assert(substitudeCommands("echo ssh ${localHost}:${localPort}", testData) ==
       "echo ssh localhorst:1234")

assert(isTableEqual(
    substitudeCommands({"-p${doesNotExist}", "2${localHost}2", "-i '${localPort}'"}, testData),
    {"-p", "2localhorst2", "-i '1234'"}
))

assert(type(lsyncd.get_free_port()) == "number")

os.exit(0)