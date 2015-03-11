local socket = require "socket"

local function client(id)
    socket.start(id)
    socket.readenable(id, true)
    print(assert(socket.read(id, "*l")))
    assert(socket.send(id, "response1\n"))
    assert(socket.send(id, "response2\n"))
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
