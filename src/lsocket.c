#include "psocket.h"
#include "alloc.h"
#include <lua.h>
#include <lauxlib.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>

#ifndef NET_LOG
#define NET_LOG printf
#endif
#define LOG NET_LOG

static lua_State *_GL; // not thread safe, make sure in single thread

static int                                        
_traceback(lua_State *L) {                        
    const char *msg = lua_tostring(L, 1);
    if (msg) luaL_traceback(L, L, msg, 1);
    else lua_pushliteral(L, "(no error message)");
    return 1;
}

static void
_dispatch(struct socket_event *event) {
    assert(_GL);
    lua_State *L = _GL;
    lua_pushcfunction(L,_traceback);
    int trace = lua_gettop(L);
    lua_rawgetp(L,LUA_REGISTRYINDEX,_dispatch);
    lua_pushinteger(L,event->type);
    lua_pushinteger(L,event->id);
    lua_pushinteger(L,event->err);
    int r = lua_pcall(L,3,0,trace);
    if (r != LUA_OK) {
        LOG("net dispatch error: %s", lua_tostring(L,-1));
        lua_pop(L,2);
    } else {
        lua_pop(L,1);
    }
}

static int
linit(lua_State *L) {
    int cmax = luaL_checkinteger(L,1);
    luaL_checktype(L,2,LUA_TFUNCTION);
    lua_rawsetp(L,LUA_REGISTRYINDEX,_dispatch);

    lua_rawgeti(L,LUA_REGISTRYINDEX,LUA_RIDX_MAINTHREAD);
    _GL = lua_tothread(L,-1);

    if (psocket_init(cmax, _dispatch) == 0) {
        lua_pushboolean(L,1);
        return 1;
    } else {
        lua_pushnil(L);
        lua_pushliteral(L,"net init fail");
        return 2;
    }
}

static int
lfini(lua_State *L) {
    psocket_fini();
    return 0;
}

static int
lpoll(lua_State *L) {
    int timeout = luaL_checkinteger(L,1);
    int n = psocket_poll(timeout);
    lua_pushinteger(L,n);
    return 1;
}

static int
llisten(lua_State *L) {
    const char *ip = luaL_checkstring(L, 1);
    int port = luaL_checkinteger(L, 2);
    int id = psocket_listen(ip, port);
    if (id >= 0) { 
        lua_pushinteger(L, id); 
        return 1;
    } else {
        lua_pushnil(L);
        lua_pushstring(L, PSOCKET_ERR);
        return 2;
    }
}

static int
lconnect(lua_State *L) {
    const char *ip = luaL_checkstring(L, 1);
    int port = luaL_checkinteger(L, 2);
    int id = psocket_connect(ip, port);
    if (id >= 0) {
        if (psocket_lasterrno() == LS_CONNECTING) {
            lua_pushinteger(L,id);
            lua_pushnil(L);
            lua_pushboolean(L,1);
            return 3;
        } else {
            lua_pushinteger(L,id);
            return 1;
        }
    } else {
        lua_pushnil(L);
        lua_pushinteger(L,psocket_lasterrno());
        return 2;
    }
}

static int
lread(lua_State *L) {
    int id = luaL_checkinteger(L, 1);
    void *data;
    int n = psocket_read(id, &data);
    if (n > 0) {
        lua_pushlightuserdata(L, data);
        lua_pushinteger(L, n);
        return 2;
    } else if (n == 0) {
        lua_pushnil(L);
        return 1;
    } else {
        lua_pushnil(L);
        lua_pushinteger(L,psocket_lasterrno());
        return 2;
    }
}

static int
lsend(lua_State *L) {
    int id = luaL_checkinteger(L,1);
    void *msg;
    int sz;
    int type = lua_type(L,2);
    switch (type) {
    case LUA_TLIGHTUSERDATA:
        msg = lua_touserdata(L,2);
        sz = luaL_checkinteger(L,3);
        break;
    case LUA_TSTRING: {
        size_t l;
        const char *s = luaL_checklstring(L,2,&l);
        int start = luaL_optinteger(L, 3, 1);
        int end = luaL_optinteger(L, 4, l);
        if (start < 1) start = 1;
        if (end > l) end = l;
        if (start > end) {
            lua_pushboolean(L, 0);
            return 1;
        }
        sz = end-start+1;
        msg = sh_malloc(sz);
        memcpy(msg, s+start-1, sz);
        break; }
    default:
        return luaL_argerror(L, 2, "invalid type");
    }
    int err = psocket_send(id,msg,sz);
    if (err != 0) lua_pushinteger(L,err);
    else lua_pushnil(L);
    return 1;
}

static int
lclose(lua_State *L) {
    int id = luaL_checkinteger(L, 1);
    int force = lua_toboolean(L, 2);
    int ok = psocket_close(id, force) == 0;
    lua_pushboolean(L, ok);
    return 1;
}

static int
lreadenable(lua_State *L) {
    int id = luaL_checkinteger(L, 1);
    int enable = lua_toboolean(L, 2);
    psocket_subscribe(id, enable);
    return 0;
}

static int
laddress(lua_State *L) {
    struct socket_addr addr;
    int id = luaL_checkinteger(L, 1); 
    if (!psocket_address(id, &addr)) {
        lua_pushstring(L, addr.ip);
        lua_pushinteger(L, addr.port);
        return 2;
    } else {
        lua_pushnil(L);
        return 1;
    }
}

static int
llimit(lua_State *L) {
    int id = luaL_checkinteger(L, 1);
    int slimit = luaL_checkinteger(L, 2);
    int rlimit = luaL_checkinteger(L, 3);
    psocket_limit(id, slimit, rlimit);
    return 0;
}

static int
lerror(lua_State *L) {
    if (lua_gettop(L) == 0)
        lua_pushstring(L, PSOCKET_ERR);
    else {
        int err = luaL_checkinteger(L, 1);
        lua_pushstring(L, psocket_error(err));
    }
    return 1;
}

int
luaopen_socket_c(lua_State *L) {
	luaL_checkversion(L);
    luaL_Reg l[] = {
        {"init", linit},
        {"fini", lfini},
        {"poll", lpoll},
        {"listen", llisten},
        {"connect", lconnect},
        {"close", lclose},
        {"read", lread},
        {"send", lsend},
        {"readenable", lreadenable},
        {"address", laddress},
        {"limit", llimit}, 
        {"error", lerror},
        {NULL, NULL},
    };
	luaL_newlib(L, l);
	return 1;
}
