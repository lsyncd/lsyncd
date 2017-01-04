#!/usr/bin/lua
require("posix")
dofile("tests/testlib.lua")

cwriteln("****************************************************************")
cwriteln(" Testing Lsyncd scheduler                                       ")
cwriteln("****************************************************************")

local tdir, srcdir, trgdir = mktemps()
local logfile = tdir .. "log"
local cfgfile = tdir .. "config.lua"
local logs =  {"-log", "all" }

writefile(cfgfile, [[
settings {
	logfile = "]]..logfile..[[",
	log = all,
	nodaemon = true,
	maxProcesses = 1
}

-- continously touches a file
acircuit = {
	delay = 0,
	onStartup = "sleep 3 && touch ^source/a",
	onCreate  = "sleep 3 && touch ^source/a",
}

-- continously touches b file
bcircuit = {
	delay = 0,
	onStartup = "sleep 3 && touch ^source/b",
	onCreate  = "sleep 3 && touch ^source/b",
}

-- continously touches c file
ccircuit = {
	delay = 0,
	onStartup = "sleep 3 && touch ^source/c",
	onCreate  = "sleep 3 && touch ^source/c",
}

sync {acircuit, source ="]]..srcdir..[[", target = "]]..trgdir..[["}
sync {bcircuit, source ="]]..srcdir..[[", target = "]]..trgdir..[["}
sync {ccircuit, source ="]]..srcdir..[[", target = "]]..trgdir..[["}
]]);


-- test if the filename exists, fails if this is different to expect
local function testfile(filename)
	local stat, err = posix.stat(filename)
	if not stat then
		cwriteln("failure: ",filename," missing")
		os.exit(1)
	end
end

cwriteln("starting Lsyncd")
local pid = spawn("./lsyncd", cfgfile, unpack(logs))
cwriteln("waiting for Lsyncd to do a few cycles")
posix.sleep(30)
cwriteln("look if every circle got a chance to run")
testfile(srcdir.."a")
testfile(srcdir.."b")
testfile(srcdir.."c")
cwriteln("killing started Lsyncd")
posix.kill(pid)
local _, exitmsg, lexitcode = posix.wait(lpid)
cwriteln("Exitcode of Lsyncd = ", exitmsg, " ", lexitcode)
posix.sleep(1);

if lexitcode == 143 then
	cwriteln("OK")
	os.exit( 0 )
else
	os.exit( 1 )
end

-- TODO remove temp
