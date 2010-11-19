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
dirsWithFileI = {}
dirsWithFileD = {}

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
			rdir[nn]=true
			if not dirsWithFileD[rdir] then
				table.insert(dirsWithFileI, rdir)
				dirsWithFileD[rdir]=true
			end
		end
	elseif acn <= 3 then
	-- moves a directory
		if #alldirs > 2 then
			-- chooses a random directory to move 
			local odir = alldirs[math.random(2, #alldirs)]
			-- chooses a random directory to move to
			local tdir = alldirs[math.random(1, #alldirs)]
			if tdir[odir.name] == nil then
				-- origin name not in target dir already
				local on = dirname(odir)
				local tn = dirname(tdir)
				cwriteln("mvdir  ",srcdir,on," -> ",srcdir,tn,odir.name)
				os.rename(srcdir..on, srcdir..tn..odir.name)
				odir.parent[odir.name] = nil
				tdir[odir.name] = odir
			end
		end
	elseif acn <= 4 then
	-- moves a file
		if #dirsWithFileI > 1 then
			-- picks a directory with a file at random
			local odir = dirsWithFileI[math.random(1, #dirsWithFileI)]
			local nf = 0
			-- counts the files in there
			for name, _ in odir do
				if #name == 2 then
					nf = nf + 1
				end
			end
			-- picks one file at random
			nf = math.random(1, nf)
			local mn 
			for name, _ in odir do
				if #name == then
					nf = nf - 1
				end
				if nf == 0 then
					mn = name
				end
			end
			-- picks a target directory at random
			local tdir = alldirs[math.random(1, #alldirs)]
			local on = dirname(odir)
			local tn = dirname(tdir)
			cwriteln("mvfile ",srcdir,on,mn," -> ",srcdir,tn,XXX  )
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

