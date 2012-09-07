-----
-- An Lsyncd+IRC-Bot Config
--
-- Logs into an IRC channel and tells there everything that happens in the
-- watched directory tree.
--
-- The challenge coding Lsyncd configs taking use of TCP sockets is
-- that they must not block! Otherwise Lsyncd will block, no longer
-- empty the kernels monitor queue, no longer collecting zombie processes,
-- no longer spawning processes (this example doesnt do any, but maybe you
-- might want to do that as well), blocking is just bad.
--
-- This demo codes just minimal IRC functionality.
-- it does not respond to anything else than IRC PING messages.
--
-- There is no flood control, if a lot happens the IRC server will disconnect
-- the bot.
--
-- Requires "luasocket" to be installed
require("socket")

-- For demo reasons, do not detach
settings.nodaemon = true
hostname = "irc.freenode.org"
--hostname = "127.0.0.1"
port = 6667
nick = "lbot01"
chan = "##lfile01"

-- this blocks until the connection is established
-- for once lets say this ok since Lsyncd didnt yet actually
-- start.
local ircSocket, err = socket.connect(hostname, port)
if not ircSocket then
	log("Error", "Cannot connect to IRC: ", err)
	terminate(-1)
end

-- from now on, the socket must not block!
ircSocket:settimeout(0)

-- Buffers for stuff to send and receive on IRC:
local ircWBuf = ""
local ircRBuf = ""

-- Predeclaration for functions calling each other
local writeIRC

-----
-- Called when the IRC socket can be written again.
-- This happens when writeIRC (see below) couldnt write
-- its buffer in one go, call it again so it can continue its task.
local function ircWritey(fd)
	writeIRC()
end

----
-- Called when there is data on the socket
local function ircReady(socket)
	local l, err, ircRBuf = ircSocket:receive("*l", ircRBuf)
	if not l then
		if err ~= "timeout" then
			log("Error", "IRC connection failed: ", err)
			terminate(-1)
		end
	else
		ircRBuf = ""
	end
	log("Normal", "ircin :", l)

	--- answers ping messages
	local ping = l:match("PING :(.*)")
	if ping then
		writeIRC("PONG :", ping, "\n")
	end
end

-----
-- Writes on IRC socket
-- Do not forget to add an "/n".
function writeIRC(...)
	-- Appends all arbuments into the write buffer
	ircWBuf = ircWBuf..table.concat({...})
	-- Gives it to the socket and sees how much it accepted
	local s, err = ircSocket:send(ircWBuf)
	-- If it cant the socket terminated.
	if not s and err~="timeout" then
		log("Error", "IRC connection failed: ", err)
		terminate(-1)
	end

	--- logs what has been send, without the linefeed.
	if (ircWBuf:sub(s, s) == "\n") then
		log("Normal", "ircout:", ircWBuf:sub(1, s - 1))
	else
		log("Normal", "ircout: ", ircWBuf:sub(1, s), "\\")
	end

	---- reduces the buffer by the amount of data sent.
	ircWBuf = ircWBuf:sub(s + 1, -1)

	-- when the write buffer is empty tell the core to no longer
	-- call ircWritey if data can be written on the socket. There
	-- is nothing to be written. If there is data in the buffer
	-- asks to be called as soon it can be written again
	if ircWBuf == "" then
		observefd(ircSocket:getfd(), ircReady, nil)
	else
		observefd(ircSocket:getfd(), ircReady, ircWritey)
	end
end

-- Aquires the nick on IRC and joins the configured channel
-- This will also register the ircReady/ircWritey function at the core
-- to be called when the socket is ready to be read/written.
writeIRC("NICK ", nick, "\n")
writeIRC("USER ", nick, " 0 * :lsyncd-sayirc-bot", "\n")
writeIRC("JOIN ", chan, "\n")

-- As action tells on IRC what the action is, then instead of
-- spawning somthing, it discards the event.
local function action(inlet)
	-- event2 is the target of a move event
	local event, event2 = inlet.getEvent()
	if not event2 then
		writeIRC("PRIVMSG ",chan," :",event.etype," ",
			event.path, "\n")
	else
		writeIRC("PRIVMSG ",chan," :",event.etype," ",
			event.path," -> ",event2.path, "\n")
	end
	inlet.discardEvent(event)
end

-- Watch a directory, and use a second for delay to aggregate events a little.
sync{source = "src",
     action = action,
	 delay  = 1,
	 onMove = true}

