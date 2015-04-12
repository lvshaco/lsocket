# lsocket

make
-----
* install lua5.2
* make && make test

test
-----
* cd test
* lua server.lua
* lua client.lua

notice
------
由于收到read事件会主动唤醒socket所在coroutine。
为了和其它类型事件coroutine协同工作（例如设备touch事件），暂时使用socket.readenable
在需要的时候手动开关系统read消息的订阅，这样带来的副作用是
如果对端重置连接无法第一时间感知以close。具体表现是：
listen端处于CLOSE_WAIT;
connect端处于FIN_WAIT_2；
占用socket槽。

所以目前的安全方式是：不和其它等待事件同时工作，让readenable常开

看看是否可以在socket lua层加个开关，指示收到read是否主动唤醒，还是cache，并且第一时间响应重置
