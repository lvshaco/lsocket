local socket = require "socket"
local tbl = require "tbl"

assert(socket.init(10))

local function fork(f,...)
    local co = coroutine.create(f)
    assert(coroutine.resume(co,...))
end

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


local function client()
    local id = assert(socket.connect("127.0.0.1",1234))
    socket.readenable(id,false)

    g_sid = id
    --read('z','key')
    --send('z','ack')
    read('z','1')
    --send('z','ack')
    send('z','2')
    read('z','2')
    print ('test--------ok')

    --assert(socket.send(id, "123456\n"))
    --socket.readenable(id,true)
    --print(assert(socket.read(id, "*l")))
    --print(assert(socket.read(id, "*l")))
end

fork(client)

while true do
    local n = socket.poll(-1)
end

print("exit")
