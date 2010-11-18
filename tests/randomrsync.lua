#!/usr/bin/lua
-- a heavy duty test.
-- makes thousends of random changes to the source tree
-- checks every X changes if lsyncd managed to keep target tree in sync.
require("posix")
dofile("tests/testlib.lua")

-- always makes the same "random", so failures can be debugged.
math.randomseed(1) 

local tdir = mktempd().."/"
cwriteln("using ", tdir, " as test root")

local srcdir = tdir.."src/"
local trgdir = tdir.."trg/"

posix.mkdir(srcdir)
posix.mkdir(trgdir)
local pid = spawn("./lsyncd","-nodaemon","-rsync",srcdir,trgdir)

cwriteln("waiting for Lsyncd to startup")
posix.sleep(1)

-- all dirs created, indexed by integer and path
adiri = {""}
adirp = {[""]=true}

cwriteln("making random data")
for ai=1,100 do
	-- throw a die what to do
	local acn = math.random(1)

	-- 1 .. creates a directory
	if acn == 1 then
		-- chooses a random directory to create it into
		local ri = math.random(#adiri)
		local rp = adiri[ri]
		local np = rp..string.char(96 + math.random(26)).."/"
		if not adirp[np] then
			-- does not yet exist.
			cwriteln("mkdir "..srcdir..np)
			posix.mkdir(srcdir..np)
			table.insert(adiri, np)
			adirp[np]=true
		end
	end
end

cwriteln("waiting for Lsyncd to finish its jobs.")
posix.sleep(20)

cwriteln("killing the Lsyncd daemon")
posix.kill(pid)
local _, exitmsg, exitcode = posix.wait(lpid)
cwriteln("Exitcode of Lsyncd = ", exitmsg, " ", exitcode)

exitcode = os.execute("diff -r "..srcdir.." "..trgdir)
cwriteln("Exitcode of diff = ", exitcode)
os.exit(exitcode)

