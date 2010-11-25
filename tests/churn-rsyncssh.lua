#!/usr/bin/lua
-- a heavy duty test.
-- makes thousends of random changes to the source tree
-- checks every X changes if lsyncd managed to keep target tree in sync.
require("posix")
dofile("tests/testlib.lua")

local tdir = mktempd().."/"
cwriteln("using ", tdir, " as test root")

local srcdir = tdir.."src/"
local trgdir = tdir.."trg/"

posix.mkdir(srcdir)
posix.mkdir(trgdir)

-- makes some startup data 
churn(srcdir, 10)

local logs = {}
--logs =  {"-log", "Inotify", "-log", "Exec" }
--logs =  {"-log", "Delay"}

local pid = spawn("./lsyncd", "-nodaemon", 
                  "-rsyncssh", srcdir, "localhost", trgdir,
                  unpack(logs))

cwriteln("waiting for Lsyncd to startup")
posix.sleep(1)

churn(srcdir, 100)

cwriteln("waiting for Lsyncd to finish its jobs.")
posix.sleep(30)

cwriteln("killing the Lsyncd daemon")
posix.kill(pid)
local _, exitmsg, lexitcode = posix.wait(lpid)
cwriteln("Exitcode of Lsyncd = ", exitmsg, " ", lexitcode)

exitcode = os.execute("diff -r "..srcdir.." "..trgdir)
cwriteln("Exitcode of diff = '", exitcode, "'")
if exitcode ~= 0 then
	os.exit(1)
else
	os.exit(0)
end


