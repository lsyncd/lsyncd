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
assert(replaceCommand("echo ssh ${localHost}:${localPort}", testData) ==
       "echo ssh localhorst:1234")

assert(isTableEqual(
    replaceCommand({"-p${doesNotExist}", "2${localHost}2", "-i '${localPort}'"}, testData),
    {"-p", "2localhorst2", "-i '1234'"}
))

os.exit(0)