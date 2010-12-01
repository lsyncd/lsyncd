require("socket")
settings.nodaemon = true
hostname = "irc.freenode.org"
--hostname = "127.0.0.1"
port = 6667
nick = "lbot01"
chan = "##lfile01"

-----
-- this blocks until the connection is established
-- for once this ok since Lsyncd didnt yet start.
local ircSocket, err = socket.connect(hostname, port)
if not ircSocket then
	log("Error", "Cannot connect to IRC: ", err)
	terminate(-1)
end

-----
-- from now it must not block!
ircSocket:settimeout(0)

------
-- Buffer for stuff to send and receive on IRC:
local ircWBuf = ""
local ircRBuf = ""
local writeIrc 

-----
-- Called when the IRC socket can be written
local function ircWritey(fd)
	writeIrc()
end

----
-- Called when socket can be read
local function ircReady(socket)
	local l, err, ircRBuf = ircSocket:receive("*l", ircRBuf)
	if not l then
		if errr ~= "timeout" then
			log("Error", "IRC connection failed: ", err)
			terminate(-1)
		end
	else
		ircRBuf = ""
		return
	end
	log("Normal", "ircin :", l)

	--- answers ping messages
	local ping = l:match("PING :(.*)")
	if ping then
		writeIRC("PONG :", ping)
	end
end

-----
-- Writes on IRC socket
function writeIrc(...)
	ircWBuf = ircWBuf..table.concat({...})
	local s, err = ircSocket:send(ircWBuf)
	if not s then
		log("Error", "IRC connection failed: ", err)
		terminate(-1)
	end
	
	--- log what has been send, but dont log the linefeed.
	if (ircWBuf:sub(s, s) == "\n") then
		log("Normal", "ircout: ", ircWBuf:sub(1, s - 1))
	else
		log("Normal", "ircout: ", ircWBuf:sub(1, s), "\\")
	end

	ircWBuf = ircWBuf:sub(s + 1, -1)

	-- when the write buffer is empty unregister from core
	-- this script no longer wants to be called when it can write
	-- on the socket. If the buffer is filled register at the core.
	if ircWBuf == "" then
		observefd(ircSocket:getfd(), ircReady, nil)
	else
		observefd(ircSocket:getfd(), ircReady, ircWritey)
	end
end

writeIrc("NICK ", nick, "\n")
writeIrc("USER ", nick, " 0 * :testbot", "\n")
writeIrc("JOIN ", chan, "\n")

----
-- Lets the Lsyncd core watch the IRCs filedescriptor
-- and call ircReady and ircWritey when they are
-- ready for transfer
observefd(ircSocket:getfd(), ircReady, ircWritey)

local function action(inlet)
	-- event2 is the target of a move event
	local event, event2 = inlet.getEvent()
	if not event2 then
		writeIrc("PRIVMSG ",chan," :",event.etype," ",
			event.path, "\n")
	else
		writeIrc("PRIVMSG ",chan," :",event.etype," ",
			event.path," -> ",event2.path, "\n")
	end
	inlet.discardEvent(event)
end



sync{source= "src", action= action, delay= 1 }


for k, v in pairs(_G) do
	print(k, v)
end
