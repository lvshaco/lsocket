#ifndef __socket_define_h__
#define __socket_define_h__

#include <stdint.h>

#define NETE_INVALID -1
#define NETE_READ    0
#define NETE_ACCEPT  1 
#define NETE_CONNECT 2 
#define NETE_CONNERR 3 
#define NETE_SOCKERR 4
#define NETE_WRIDONECLOSE 5
//#define NETE_WRIDONE 5
#define NETE_CONN_THEN_READ 6
#define NETE_TIMEOUT 7
#define NETE_LOGOUT 8
#define NETE_REDISREPLY 9

struct net_event {
    int id;
    int type;     // see NETE
    int udata;
    union {
        int err;
        int listenid;
    };
};

// must be negative, positive for system error number
//#define OK 0
#define NET_ERR_EOF         -1
#define NET_ERR_MSG         -2
#define NET_ERR_NOSOCK      -3
#define NET_ERR_CREATESOCK  -4
#define NET_ERR_WBUFOVER    -5
#define NET_ERR_NOBUF       -6
#define NET_ERR_LISTEN      -7
#define NET_ERR_CONNECT     -8
#define NET_CONNECTING      -9

struct net_addr {
    char ip[40];
    uint16_t port;
};

#endif
