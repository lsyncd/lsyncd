#!/usr/bin/lua
-- a heavy duty test.
-- makes thousends of random changes to the source tree
-- checks every X changes if lsyncd managed to keep target tree in sync.
require("posix")
dofile("tests/testlib.lua")

cwriteln("****************************************************************")
cwriteln(" Testing default.rsync with random data activity ")
cwriteln("****************************************************************")

local tdir, srcdir, trgdir = mktemps()

-- makes some startup data 
churn(srcdir, 10)

local logs = {}
--logs =  {"-log", "Delay", "-log", "Fsevents" }
local pid = spawn("./lsyncd", "-nodaemon", "-delay", "5",
                  "-rsync", srcdir, trgdir, unpack(logs))

cwriteln("waiting for Lsyncd to startup")
posix.sleep(1)

churn(srcdir, 500)

cwriteln("waiting for Lsyncd to finish its jobs.")
posix.sleep(10)

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


