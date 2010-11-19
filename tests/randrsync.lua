#!/usr/bin/lua
-- a heavy duty test.
-- makes thousends of random changes to the source tree
-- checks every X changes if lsyncd managed to keep target tree in sync.
require("posix")
dofile("tests/testlib.lua")

-- always makes the same "random", so failures can be debugged.
math.randomseed(6) 

local tdir = mktempd().."/"
cwriteln("using ", tdir, " as test root")

local srcdir = tdir.."src/"
local trgdir = tdir.."trg/"

posix.mkdir(srcdir)
posix.mkdir(trgdir)
-- local pid = spawn("./lsyncd","-nodaemon","-rsync",srcdir,trgdir)

cwriteln("waiting for Lsyncd to startup")
posix.sleep(1)

-- all dirs created, indexed by integer and path
root = {name=""}
alldirs = {root}

-----
-- returns the name of a directory
-- call it with name=nil
local function dirname(dir, name)
	name = name or ""
	if not dir then
		return name
	end
	return dirname(dir.parent, dir.name .. "/" .. name)
end


cwriteln("making random data")
for ai=1,10 do
	-- throw a die what to do
	local acn = math.random(3)

	if acn <= 1 then 
	-- creates a directory
		-- chooses a random directory to create it into
		local rdir = alldirs[math.random(#alldirs)]
		-- creates a new random one letter name
		local nn = string.char(96 + math.random(26))
		if not rdir[nn] then
			local ndir = {
				name   = nn,
				parent = rdir, 
			}
			local dn = dirname(ndir)
			rdir[nn] = dn
			table.insert(alldirs, ndir)
			cwriteln("mkdir  "..srcdir..dn)
			posix.mkdir(srcdir..dn)
		end
	elseif acn <= 2 then
	-- creates a file
		-- chooses a random directory to create it into
		local rdir = alldirs[math.random(#alldirs)]
		-- creates a new random one letter name
		local nn = 'f'..string.char(96 + math.random(26))
		local fn = dirname(rdir) .. nn
		cwriteln("mkfile "..srcdir..fn)
		local f = io.open(srcdir..fn, "w")
		if f then
			for i=1,10 do
				f:write(string.char(96 + math.random(26)))
			end
			f:write('\n')
			f:close()
		end
	elseif acn <= 3 then
	-- moves a directory
		if #alldirs > 2 then
			-- chooses a random directory to move 
			local odir = alldirs[math.random(2, #alldirs)]
			-- chooses a random directory to move to
			local tdir = alldirs[math.random(2, #alldirs)]
			if tdir[odir.name] == nil then
				-- origin name not in target dir already
				local on = dirname(odir)
				local tn = dirname(tdir)
				cwriteln("mvdir  "..srcdir..on, " -> ", srcdir..tn)
				os.rename(srcdir..on, srcdir..tn..odir.name)
				odir.parent[odir.name] = nil
				tdir[odir.name] = odir
			end
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

