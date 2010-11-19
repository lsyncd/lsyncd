#!/usr/bin/lua
-- a heavy duty test.
-- makes thousends of random changes to the source tree
-- checks every X changes if lsyncd managed to keep target tree in sync.
require("posix")
dofile("tests/testlib.lua")

-- always makes the same "random", so failures can be debugged.
math.randomseed(2) 

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

-----
-- Picks a random dir.
--
local function pickDir(notRoot)
	if notRoot then
		if #alldirs <= 2 then
			return nil
		end
		return alldirs[2, math.random(#alldirs)]
	end
	return alldirs[math.random(#alldirs)]
end

----
-- Picks a random file.
--
-- Returns 3 values: 
--  * the directory
--  * the filename
--  * number of files in directory
--
local function pickFile()
	-- picks the random directory
	if #dirsWithFileI < 1 then
		return
	end
	local rdir = dirsWithFileI[math.random(1, #dirsWithFileI)]
	if not rdir then
		return
	end

	-- counts the files in there
	local c = 0
	for name, _ in pairs(rdir) do
		if #name == 2 then
			c = c + 1
		end
	end

	-- picks one file at random
	local cr = math.random(1, c)
	local fn 
	for name, _ in pairs(rdir) do
		if #name == 2 then
			-- filenames are 2 chars wide.
			cr = cr - 1
			if cr == 0 then
				fn = name
				break
			end
		end
	end
	return rdir, fn, c
end

-----
-- Removes a reference to a file
--
-- @param dir  --- directory reference
-- @param fn   --- filename
-- @param c    --- number of files in dir
--
local function rmFileReference(dir, fn, c)
	dir[fn] = nil
	if c == 1 then
		-- if last file from origin dir, it has no files anymore
		for i, v in ipairs(dirsWithFileI) do
			if v == dir then 
				table.remove(dirsWithFileI, i)
				break
			end
		end
		dirsWithFileD[dir] = nil
	end
end

cwriteln("making random data")
for ai=1,15 do
	-- throw a die what to do
	local acn = math.random(5)

	if acn <= 1 then 
	-- creates a directory
		-- chooses a random directory to create it into
		local rdir = pickDir()
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
		local rdir = pickDir()
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
			local odir = pickDir()
			-- chooses a random directory to move to
			local tdir = pickDir(true)
			if tdir[odir.name] == nil then
				-- origin name not in target dir already
				local on = dirname(odir)
				local tn = dirname(tdir)
				cwriteln("mvdir  ",srcdir,on," -> ",srcdir,tn,odir.name)
				os.rename(srcdir..on, srcdir..tn..odir.name)
				odir.parent[odir.name] = nil
				odir.parent = tdir
				tdir[odir.name] = odir
			end
		end
	elseif acn <= 4 then
	-- moves a file
		local odir, fn, c = pickFile()
		if odir then
			-- picks a directory with a file at random
			-- picks a target directory at random
			local tdir = pickdir()
			local on = dirname(odir)
			local tn = dirname(tdir)
			cwriteln("mvfile ",srcdir,on,mn," -> ",srcdir,tn,mn)
			os.rename(srcdir..on..mn, srcdir..tn..mn)
			rmFileReference(odir, mn, c)
			tdir[mn] = true
			if not dirsWithFileD[tdir] then
				dirsWithFileD[tdir] = true
				table.insert(dirsWithFileI, tdir)
			end
		end
	elseif acn <= 5 then
	-- removes a file
		local dir, fn, c = pickFile()
		if dir then
			local dn = dirname(dir)
			cwriteln("rmfile ",srcdir,dn,fn)
			posix.unlink(srcdir..dn..fn)
			rmFileReference(odir, mn, c)
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

