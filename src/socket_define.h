#ifndef __socket_define_h__
#define __socket_define_h__

#include <stdint.h>

#define LS_PROTOCOL_TCP 0
#define LS_PROTOCOL_UDP 1
#define LS_PROTOCOL_IPC 2

#define LS_EINVALID -1
#define LS_EREAD    0
#define LS_EACCEPT  1 
#define LS_ECONNECT 2 
#define LS_ECONNERR 3 
#define LS_ESOCKERR 4
#define LS_EWRIDONECLOSE 5
#define LS_ECONN_THEN_READ 6
#define LS_EREAD0 7

struct socket_event {
    int id;
    int type;     // see LS_E 
    int udata;
    union {
        int err;
        int listenid;
    };
};

// must be negative, positive for system error number
//#define OK 0
#define LS_ERR_EOF         -1
#define LS_ERR_MSG         -2
#define LS_ERR_NOSOCK      -3
#define LS_ERR_CREATESOCK  -4
#define LS_ERR_WBUFOVER    -5
#define LS_ERR_NOBUF       -6
#define LS_ERR_LISTEN      -7
#define LS_ERR_CONNECT     -8
#define LS_CONNECTING      -9
#define LS_ERR_TRUNC       -10
#define LS_ERR_CMSGTYPE    -11
#define LS_ERR_STATUS      -12

struct socket_addr {
    char ip[40];
    uint16_t port;
};

#endif
