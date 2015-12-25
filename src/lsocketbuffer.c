#include "alloc.h"
#include <lua.h>
#include <lauxlib.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>

#define METANAME "SBUF*"

// socket buffer
struct buffer_node {
    char *p;
    int sz;
    struct buffer_node *next;
};

struct socket_buffer {
    int size;
    int offset;
    struct buffer_node *head;
    struct buffer_node *tail; 
};

static int
lfree(struct lua_State *L) {
    luaL_checktype(L, 1, LUA_TUSERDATA);
    struct socket_buffer *sb = lua_touserdata(L, 1);
    while (sb->head) {
        struct buffer_node *next = sb->head->next;
        free(sb->head->p);
        free(sb->head);
        sb->head = next;
    }
    return 0;
}

static int
lnew(struct lua_State *L) {
    struct socket_buffer *sb = lua_newuserdata(L, sizeof(*sb));
    sb->size = 0;
    sb->offset = 0;
    sb->head = NULL;
    sb->tail = NULL;
    luaL_setmetatable(L, METANAME);
    return 1;
}

static int
lpush(struct lua_State *L) {
    luaL_checktype(L, 1, LUA_TUSERDATA);
    struct socket_buffer *sb = lua_touserdata(L, 1);
    luaL_checktype(L, 2, LUA_TLIGHTUSERDATA);
    void *p = lua_touserdata(L, 2);
    int sz = luaL_checkinteger(L, 3);
    if (!p || sz <= 0) {
        lua_pushnil(L);
        return 1;
    }
    struct buffer_node *node = malloc(sizeof(*node));
    node->p = p;
    node->sz = sz;
    node->next = NULL;
    if (sb->head == NULL) {
        sb->head = node;
        sb->tail = node;
    } else {
        assert(sb->tail);
        sb->tail->next = node;
        sb->tail = node;
    }
    sb->size += sz;
    lua_pushinteger(L, sb->size);
    return 1;
}

static void
pushpack(struct lua_State *L, 
         struct socket_buffer *sb, 
         struct buffer_node *node, int end) {
    luaL_Buffer b;
    luaL_buffinit(L, &b);
    struct buffer_node *current = sb->head;
    int offset = sb->offset;
    while (current != node) {
        luaL_addlstring(&b, current->p+offset, current->sz-offset);
        current = current->next;
        offset = 0;
    }
    luaL_addlstring(&b, current->p+offset, end-offset);
    luaL_pushresult(&b);
}

static void
freebuffer(struct socket_buffer *sb, 
           struct buffer_node *node, int end) {
    sb->size += sb->offset;
    struct buffer_node *tmp;
    for (;;) {
        if (sb->head == node) {
            if (node->sz == end) {
                sb->head = sb->head->next;
                sb->size -= node->sz;
                sb->offset = 0;
                free(node->p);
                free(node);
            } else {
                sb->size -= end;
                sb->offset = end;
            }
            return;
        } else {
            tmp = sb->head;
            sb->head = sb->head->next;
            sb->size -= tmp->sz;
            free(tmp->p);
            free(tmp);
        }
    }
}

static struct buffer_node *
checksep(struct buffer_node *node, 
         int offset, 
         const char *sep, int l, int *end) {
    int n=0;
    do {
        int sz = node->sz-offset;
        if (sz > l)
            sz = l;
        if (memcmp(node->p+offset, sep+n, sz))
            return NULL;
        n += sz;
        if (n>=l) {
            *end = offset+sz;
            return node;
        }
        offset = 0;
        node = node->next;
    } while (node);
    return NULL;
}

static int
readsep(struct lua_State *L, 
        struct socket_buffer *sb, 
        const char *sep, int l) {
    struct buffer_node *current = sb->head;
    struct buffer_node *end_node;
    int offset = sb->offset;
    int end, i;
    while (current) {
        for (i=offset; i<current->sz; ++i) {
            end_node = checksep(current, i, sep, l, &end);
            if (end_node) {
                pushpack(L, sb, current, i);
                freebuffer(sb, end_node, end);
                return 1;
            }
        }
        current = current->next;
        offset = 0;
    }
    lua_pushnil(L);
    return 1;
}

static int
readall(struct lua_State *L, 
        struct socket_buffer *sb) {
    if (sb->head) {
        struct buffer_node *node = sb->tail;
        pushpack(L, sb, node, node->sz);
        freebuffer(sb, node, node->sz);
        assert(sb->size == 0);
        assert(sb->offset == 0);
        return 1;
    } else {
        lua_pushnil(L);
        return 1;
    }
}
/*
static int
readhead(struct lua_State *L, 
         struct socket_buffer *sb, int n) {
    assert(n > 0);
    if (sb->size < n) {
        lua_pushnil(L);
        return 1;
    }
    uint32_t head = 0;
    struct buffer_node *current = sb->head;
    int offset = sb->offset;
    int i=0, o;
    while (current) {
        for (o=offset; o<current->sz; ++o) {
            head |= ((uint8_t*)current->p)[o]<<((i++)*8); 
            if (i>=n) {
                freebuffer(sb, current, o+1);
                lua_pushinteger(L, head);
                return 1;
            }
        }
        current = current->next;
        offset = 0;
    }
    lua_pushnil(L);
    return 1;
}
*/
static int
readn(struct lua_State *L, 
      struct socket_buffer *sb, int n) {
    if (n==0) {
        lua_pushliteral(L, "");
        return 1;
    }
    if (sb->size < n) {
        lua_pushnil(L);
        return 1;
    }
    struct buffer_node *current = sb->head;
    int offset = sb->offset;
    int sz = 0;
    while (current) {
        sz += current->sz-offset;
        if (sz >= n) {
            int end = current->sz-(sz-n);
            pushpack(L, sb, current, end);
            freebuffer(sb, current, end);
            return 1;
        }
        current = current->next;
        offset = 0;
    }
    lua_pushnil(L);
    return 1;
}

static int
recvmsg(struct lua_State *L,
        struct socket_buffer *sb) {
    if (sb->head == NULL) {
        lua_pushnil(L);
        return 1;
    }
    struct buffer_node *node = sb->head;
    int size = node->sz;
    if (size < 4) {
        sb->head = node->next;
        free(node->p);
        free(node);
        return luaL_error(L, "Invalid data format");
    }
    char *p = node->p;
    int fd = *(int*)p;
    if (size == 4) {
        lua_pushinteger(L, fd);
        sb->head = node->next;
        free(node->p);
        free(node);
        return 1;
    } else {
        lua_pushinteger(L, fd);
        lua_pushlstring(L, p+4, size-4);
        sb->head = node->next;
        free(node->p);
        free(node);
        return 2;
    }
}

static int
lfindsep(struct lua_State *L) {
    luaL_checktype(L, 1, LUA_TUSERDATA);
    struct socket_buffer *sb = lua_touserdata(L,1);
    size_t l;
    const char *sep = luaL_checklstring(L, 2, &l);
    struct buffer_node *current = sb->head;
    struct buffer_node *end_node;
    int offset = sb->offset;
    int end, i;
    while (current) {
        for (i=offset; i<current->sz; ++i) {
            end_node = checksep(current, i, sep, l, &end);
            if (end_node) {
                lua_pushboolean(L, 1);
                return 1;
            }
        }
        current = current->next;
        offset = 0;
    }
    lua_pushnil(L);
    return 1;
}

static int
lpop(struct lua_State *L) {
    luaL_checktype(L, 1, LUA_TUSERDATA);
    struct socket_buffer *sb = lua_touserdata(L, 1); 
    int nargs = lua_gettop(L);
    if (nargs == 1) {
        return readall(L, sb);
    } else {
        int type = lua_type(L, 2);
        switch (type) {
        case LUA_TSTRING: {
            size_t l;
            const char *sep = luaL_checklstring(L, 2, &l);
            if (l>0) return readsep(L, sb, sep, l);
            else return luaL_argerror(L, 2, "invalid sep");
            }
        case LUA_TNUMBER: {
            uint32_t n = luaL_checkinteger(L, 2);
            return readn(L, sb, n);
            } 
        case LUA_TNIL:
            return recvmsg(L, sb);
        default:
            return readall(L, sb);
            //return luaL_argerror(L, 2, "invalid mode");
        }
    }
}

static int
ldetach(struct lua_State *L) {
    luaL_checktype(L, 1, LUA_TUSERDATA);
    struct socket_buffer *sb = lua_touserdata(L, 1); 
    if (sb->size <= 0) {
        lua_pushnil(L);
        return 1;
    } 
    size_t buf_size = sb->size;
    void *p = malloc(buf_size);
    int diff = 0;
    struct buffer_node *current = sb->head;
    int offset = sb->offset;
    while (current) {
        assert(diff < sb->size);
        memcpy(p+diff, current->p+offset, current->sz-offset);
        diff += current->sz-offset;
        current = current->next;
        offset = 0;
    }
    assert(diff==sb->size);

    freebuffer(sb, sb->tail, sb->size);

    lua_pushlightuserdata(L,p);
    lua_pushinteger(L,buf_size);
    return 2;
}

//static void
//pushpackp(struct lua_State *L, 
//         struct socket_buffer *sb, int n,
//         struct buffer_node *node, int end) {
//    char *pack = malloc(n);
//    char *p = pack;
//    struct buffer_node *current = sb->head;
//    int offset = sb->offset, diff;
//    while (current != node) {
//        diff = current->sz-offset;
//        memcpy(p, current->p+offset, diff);
//        p += diff;
//        current = current->next;
//        offset = 0;
//    }
//    diff = end-offset;
//    memcpy(p, current->p+offset, diff);
//    p += diff;
//    assert(p-pack == n);
//    lua_pushlightuserdata(L, pack);
//    lua_pushinteger(L, n);
//}

//static int
//readnp(struct lua_State *L, 
//      struct socket_buffer *sb, int n) {
//    if (n==0) {
//        lua_pushlightuserdata(L, NULL);
//        lua_pushinteger(L, 0);
//        return 2;
//    }
//    if (sb->size < n) {
//        lua_pushnil(L);
//        return 1;
//    }
//    struct buffer_node *current = sb->head;
//    int offset = sb->offset;
//    int sz = 0;
//    while (current) {
//        sz += current->sz-offset;
//        if (sz >= n) {
//            int end = current->sz-(sz-n);
//            pushpackp(L, sb, n, current, end);
//            freebuffer(sb, current, end);
//            return 2;
//        }
//        current = current->next;
//        offset = 0;
//    }
//    lua_pushnil(L);
//    return 1;
//}

//static int
//lpopbytes(struct lua_State *L) {
//    luaL_checktype(L, 1, LUA_TUSERDATA);
//    struct socket_buffer *sb = lua_touserdata(L, 1); 
//    uint32_t n = luaL_checkinteger(L, 2);
//    return readnp(L, sb, n);
//}
//
//static int
//lfreebytes(struct lua_State *L) {
//    luaL_checktype(L,1,LUA_TLIGHTUSERDATA);
//    void *p = lua_touserdata(L,1);
//    free(p);
//    return 0;
//}

static void
createmeta(struct lua_State *L) {
    luaL_Reg l[] = {
        {"push", lpush},
        {"pop", lpop},
        {"findsep", lfindsep},
        {"detach", ldetach},
        //{"popbytes", lpopbytes},
        //{"freebytes", lfreebytes },
        {"__gc", lfree},
        {NULL, NULL},
    };
    luaL_newmetatable(L, METANAME);
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");
    luaL_setfuncs(L, l, 0);
    lua_pop(L, 1);
}

int
luaopen_socketbuffer_c(lua_State *L) {
	luaL_checkversion(L);
    luaL_Reg l[] = {
        {"new", lnew},
        {"push", lpush},
        {"pop", lpop},
        {"findsep", lfindsep},
        {"detach", ldetach},
        //{"popbytes", lpopbytes},
        //{"freebytes", lfreebytes },
        {NULL, NULL},
    };
	luaL_newlib(L, l);
    createmeta(L);
	return 1;
}
