#!/usr/bin/lua
-- a heavy duty test.
-- makes thousends of random changes to the source tree
-- checks every X changes if lsyncd managed to keep target tree in sync.

require("posix")

-- always makes the same "random", so failures can be debugged.
math.randomseed(1) 

-- escape codes to colorize output on terminal
local c1="\027[47;34m"
local c0="\027[0m"

---
-- writes colorized
--
function cwriteln(...)
	io.write(c1)
	io.write(...)
	io.write(c0, "\n")
end

cwriteln("******************************************************************")
cwriteln("* heavy duty tests duing randoms                                 *")
cwriteln("******************************************************************")

local lpid = posix.fork()
if lpid < 0 then
	cwriteln("ERROR: failed fork!")
	os.exit(-1);
end
if lpid == 0 then
	posix.exec("./lsyncd", "-nodaemon", "-log", "all", "-rsync", "src", "trg")
	-- should not return
	cwriteln("ERROR: failed to spawn lysncd!")
	os.exit(-1);
end

-- cleans the targets
os.execute("rm -rf src/*")
os.execute("rm -rf trg/*")
posix.sleep(1)


while true do
	-- throw a die what to do
	local acn = math.random(2)
	if acn == 1 then
		-- creates a directory
		print(string.char(96))
	end
	if acn == 2 then
		-- creates a files
	end

end

-- kills the lsyncd daemon
posix.kill(lpid)
local _, exitmsg, exitcode = posix.wait(lpid)
cwriteln("Exitcode of Lsyncd = ", exitmsg, " ", exitcode)
os.exit(lexitcode)

