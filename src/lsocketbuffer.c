#include <lua.h>
#include <lauxlib.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>

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
lnew(struct lua_State *L) {
    struct socket_buffer *sb = lua_newuserdata(L, sizeof(*sb));
    sb->size = 0;
    sb->offset = 0;
    sb->head = NULL;
    sb->tail = NULL;
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
        return 0;
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
    return 0;
}

static void
pushpackp(struct lua_State *L, 
         struct socket_buffer *sb, int n,
         struct buffer_node *node, int end) {
    char *pack = malloc(n);
    char *p = pack;
    struct buffer_node *current = sb->head;
    int offset = sb->offset, diff;
    while (current != node) {
        diff = current->sz-offset;
        memcpy(p, current->p+offset, diff);
        p += diff;
        current = current->next;
        offset = 0;
    }
    diff = end-offset;
    memcpy(p, current->p+offset, diff);
    p += diff;
    assert(p-pack == n);
    lua_pushlightuserdata(L, pack);
    lua_pushinteger(L, n);
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
                lua_pushunsigned(L, head);
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
readnp(struct lua_State *L, 
      struct socket_buffer *sb, int n) {
    if (n==0) {
        lua_pushlightuserdata(L, NULL);
        lua_pushinteger(L, 0);
        return 2;
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
            pushpackp(L, sb, n, current, end);
            freebuffer(sb, current, end);
            return 2;
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
        return readsep(L, sb, "\n", 1);
    } else {
        int type = lua_type(L, 2);
        switch (type) {
        case LUA_TSTRING: {
            size_t l;
            const char *p = luaL_checklstring(L, 2, &l);
            if (l>1) {
                if (p[0] == '*') {
                    switch (p[1]) {
                    case '1': return readhead(L, sb, 1);
                    case '2': return readhead(L, sb, 2);
                    case '4': return readhead(L, sb, 4);
                    case 'a': return readall(L, sb);
                    case 'l': return readsep(L, sb, "\n", 1);
                    default:
                        return luaL_argerror(L, 2, "invalid mode");
                    }
                } else {
                    return readsep(L, sb, p, l);
                }
            } else if (l==1) {
                return readsep(L, sb, p, l);
            } else {
                return luaL_argerror(L, 2, "invalid mode");
            }}
        case LUA_TNUMBER: {
            uint32_t n = luaL_checkunsigned(L, 2);
            return readn(L, sb, n);
            } 
        default:
            return luaL_argerror(L, 2, "invalid format");
        }
    }
}

static int
lpopbytes(struct lua_State *L) {
    luaL_checktype(L, 1, LUA_TUSERDATA);
    struct socket_buffer *sb = lua_touserdata(L, 1); 
    uint32_t n = luaL_checkunsigned(L, 2);
    return readnp(L, sb, n);
}

static int
lfreebytes(struct lua_State *L) {
    luaL_checktype(L,1,LUA_TLIGHTUSERDATA);
    void *p = lua_touserdata(L,1);
    free(p);
    return 0;
}

int
luaopen_socketbuffer_c(lua_State *L) {
	luaL_checkversion(L);
    luaL_Reg l[] = {
        {"new", lnew},
        {"push", lpush},
        {"pop", lpop},
        {"popbytes", lpopbytes},
        {"freebytes", lfreebytes },
        {NULL, NULL},
    };
	luaL_newlib(L, l);
	return 1;
}
