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
userenv =
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
	signum = signum,
	string = string,
	table = table,
	type = type,
	unpack = unpack,

	-- lsyncd mantle available to user scripts
	Array = Array,
	Queue = Queue,

	-- user interface functions and objects
	alarm = user.alarm,
	nonobservefs = user.nonobserfd,
	observefd = user.observefd,
	settings = user.settings,
	signal = signal,
	spawn = user.spawn,
	spawnShell = user.spawnShell,
	sync = user.sync,
	syncs = user.syncs,

	-- lsyncd core available to user scripts
	-- FIXME always make wrappers
	log = core.log,
	now = core.now,
	readdir = core.readdir,
	terminate = core.terminate
}

userenv._G = userenv

