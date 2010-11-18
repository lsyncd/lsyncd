-- common testing environment

require("posix")

-- escape codes to colorize output on terminal
local c1="\027[47;34m"
local c0="\027[0m"

---
-- writes colorized
--
function cwriteln(...)
	io.write(c1, ...)
	io.write(c0, "\n")
end

-----
-- creates a tmp directory
function mktempd()
	local f = io.popen('mktemp -d', 'r')
	local s = f:read('*a')
	f:close()
	s = s:gsub('[\n\r]+', ' ')
	s = s:match("^%s*(.-)%s*$")
	return s
end

-----
-- spawns a subprocess.
--
function spawn(...)
	local pid = posix.fork()
	if pid < 0 then
		cwriteln("Error, failed fork!")
		os.exit(-1)
	end
	if lpid == 0 then
		posix.exec(...)
		-- should not return
		cwriteln("Error, failed to spawn: ", ...)
		os.exit(-1);
	end
	return pid
end

print(mktempd())

