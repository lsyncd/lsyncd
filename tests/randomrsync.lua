#!/usr/bin/lua
-- a heavy duty test.
-- makes thousends of random changes to the source tree
-- checks every X changes if lsyncd managed to keep target tree in sync.
require("posix")
dofile("tests/testlib.lua")

-- always makes the same "random", so failures can be debugged.
math.randomseed(1) 

local tdir = mktempd()
cwriteln("using ", tdir, " as test root")

local srcdir = tdir.."src/"
local trgdir = tdir.."trg/"

posix.mkdir(srcdir)
posix.mkdir(trgdir)
spawn("./lsyncd","-nodaemon","-log","all","-rsync",srcdir,trgdir)

-- lets Lsyncd startup
posix.sleep(1)

-- all dirs created, indexed by integer and path
adiri = {""}
adirp = {[""]=true}

for ai=1,100 do
	-- throw a die what to do
	local acn = math.random(1)

	-- 1 .. creates a directory
	if acn == 1 then
		-- chooses a random directory to create it into
		local ri = math.random(adirs.size())
		local rp = adiri[dn]
		local np = rp..string.char(96 + math.random(26)).."/"
		if not adirp[np] then
			-- does not yet exist.
			posix.mkdir(np)
			table.insert(adiri, np)
			adirp[np]=true
		end
	end
end



-- kills the lsyncd daemon
-- posix.kill(lpid)
-- local _, exitmsg, exitcode = posix.wait(lpid)
-- cwriteln("Exitcode of Lsyncd = ", exitmsg, " ", exitcode)
-- os.exit(lexitcode)

