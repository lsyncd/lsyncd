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

local function testQueue()
    local q = Queue.new()
    q:push(1)
    q:push(2)
    q:push(3)
    q:push(4)
    assert(q:size(), 4)
    assert(q[1], 1)
    assert(q[4], 4)

    q:remove(4)
    assert(q:size(), 3)
    assert(q[3], 3)
    assert(q[1], 1)

    q:remove(1)
    assert(q:size(), 2)
    assert(q[3], 3)
    assert(q[2], 2)
    assert(q.first, 2)
    assert(q.last, 3)

    q:push(5)
    assert(q:size(), 3)
    assert(q.last, 4)
    assert(q.first, 2)
    assert(q[4], 5)
    assert(q[3], 3)
    assert(q[2], 2)

    q:remove(3)
    assert(q:size(), 2)
    assert(q.last, 3)
    assert(q.first, 2)
    assert(q[2], 2)
    assert(q[3], 5)

    q:inject(23)
    assert(q:size(), 3)
    assert(q.last, 3)
    assert(q.first, 1)
    assert(q[1], 23)
    assert(q[2], 2)
    assert(q[3], 5)
end

testQueue()

os.exit(0)