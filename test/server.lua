local socket = require "socket"
local tbl = require "tbl"

local g_sid
local function read(fmt,cmd)
    socket.readenable(g_sid, true)
    print ('wait to read:',cmd)
    local sz = assert(socket.read(g_sid,'*1'))
    print ('------ read sz',sz)
    local s = assert(socket.read(g_sid,sz))
    socket.readenable(g_sid, false)
    local t = table.pack(string.unpack(fmt,s))
    assert(t[1]==cmd, string.format('read %s, but desire %s',t[1],cmd))
    tbl.print(t,'------ read:'..cmd)
    return select(2,table.unpack(t))
end

local function send(fmt, ...)
    local t = {...}
    local s = string.pack(fmt,...)
    assert(#s<256)
    s = string.char(#s)..s
    assert(socket.send(g_sid, s))
    tbl.print(t, 'send:')
end


local function client(id)
    socket.start(id)

    g_sid = id
    --send('z','key')
    --read('z','ack')
    send('z','1')
    --read('z','ack')
    send('z','2')
    read('z','2')
    print ('test--------ok')

    --socket.readenable(id, true)
    --print(assert(socket.read(id, "*l")))
    --assert(socket.send(id, "response1\n"))
    --[[assert(socket.send(id, "response2\n"))]]
end

local function fork(f,...)
    local co = coroutine.create(f)
    assert(coroutine.resume(co,...))
end

assert(socket.init(10))

local lid = assert(socket.listen("127.0.0.1", 1234))
socket.start(lid, function(id)
    fork(client, id)
end)

while true do
    local n = socket.poll(-1)
end

print("exit")
