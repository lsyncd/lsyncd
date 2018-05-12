--
-- userenv.lua from Lsyncd -- the Live (Mirror) Syncing Demon
--
--
-- Setups up the global environment for a user script.
--
-- The default sync implementations will add the 'default' global
-- to this. They are loaded in user context, so they can simply set it.
--
--
-- License: GPLv2 (see COPYING) or any later version
-- Authors: Axel Kittenberger <axkibe@gmail.com>
--
userENV =
{
	-- generic lua stuff to be available
	_VERSION = _VERSION,
	assert = assert,
	bit32 = bit32,
	coroutine = coroutine,
	dofile = dofile,
	error = error,
	getmetatable = getmetable,
	getsignal = getsignal,
	io = io,
	ipairs = ipairs,
	load = load,
	loadfile = loadfile,
	loadstring = loadstring,
	math = math,
	module = module,
	next = next,
	onsignal = onsignal,
	os = os,
	package = package,
	pairs = pairs,
	pcall = pcall,
	print = print,
	rawequal = rawequal,
	rawlen = rawlen,
	rawget = rawget,
	rawset = rawset,
	require = require,
	select = select,
	setmetatable = setmetatable,
	string = string,
	table = table,
	type = type,
	unpack = unpack,

	-- lsyncd mantle available to user scripts
	Array = Array,
	Queue = Queue,
	settings = settings,
	spawn = spawn,
	spawnShell = spawnShell,
	sync = sync,

	-- lsyncd core available to user scripts
	log = core.log,
	nonobservefs = core.nonobserfd,
	now = core.now,
	observefd = core.observefd,
	readdir = core.readdir,
	terminate = core.terminate
}

userENV._G = userENV

