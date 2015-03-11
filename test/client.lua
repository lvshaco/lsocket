local socket = require "socket"

assert(socket.init(10))

local function fork(f,...)
    local co = coroutine.create(f)
    assert(coroutine.resume(co,...))
end

local function client()
    local id = assert(socket.connect("127.0.0.1",1234))
    assert(socket.send(id, "123456\n"))
    socket.readenable(id,true)
    print(assert(socket.read(id, "*l")))
    print(assert(socket.read(id, "*l")))
end

fork(client)

while true do
    local n = socket.poll(-1)
end

print("exit")
