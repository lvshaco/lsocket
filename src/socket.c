#include "alloc.h"
#include "socket.h"
#include "socket_platform.h"
#include "np.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>

#define STATUS_INVALID    -1
#define STATUS_LISTENING   1 
#define STATUS_CONNECTING  2
#define STATUS_CONNECTED   3
#define STATUS_HALFCLOSE   4
#define STATUS_SUSPEND     5
#define STATUS_OPENED      STATUS_LISTENING
#define STATUS_BIND        6

#define LISTEN_BACKLOG 511
#define RBUFFER_SZ 64
#define RECVMSG_MAXSIZE 64

#define ERR(err) (err) != 0 ? (err) : LS_ERR_EOF;

static const char *STRERROR[] = {
    "",
    "net error end of file",
    "net error msg",
    "net error no socket",
    "net error create socket",
    "net error write buffer over",
    "net error no buffer",
    "net error listen",
    "net error connect",
    "net connecting",
    "ipc trunc",
    "recvmsg control type error",
    "net error status",
};

struct sbuffer {
    struct sbuffer *next;
    int sz;
    int fd; // for ipc
    char *begin;
    char *ptr;
};

struct socket {
    socket_t fd;
    int protocol;
    int status;
    int mask;
    int udata;
    struct sbuffer *head;
    struct sbuffer *tail; 
    int sbuffersz;
    int rbuffersz;
    int slimit; 
    int rlimit;
};

struct net {
    struct np_state np;
    int max;
    int err;
    struct np_event  *i_events;
    struct socket_event *o_events; 
    struct socket *sockets;
    struct socket *free_socket;
    struct socket *tail_socket;
    char recvmsg_buffer[RECVMSG_MAXSIZE];
};

static inline struct socket *
_socket(struct net *self, int id) {
    assert(id>=0 && id<self->max);
    struct socket *s = &self->sockets[id];
    if (s->status != STATUS_INVALID)
        return s;
    else {
        self->err = LS_ERR_NOSOCK;
        return NULL;
    }
}

static int
_subscribe(struct net *self, struct socket *s, int mask) {
//fprintf(stderr, "pid=%d, socket subscribe:%d, mask=%d\n",(int)getpid(), (int)(s-self->sockets), mask);
    int result;
    if (mask == s->mask)
        return 0;
    if (mask == 0)
        result = np_del(&self->np, s->fd);
    else if (s->mask == 0)
        result = np_add(&self->np, s->fd, mask, s);
    else
        result = np_mod(&self->np, s->fd, mask, s);
    if (result == 0)
        s->mask = mask;
    return result;
}

static struct socket*
_alloc_sockets(int max) {
    assert(max > 0);
    int i;
    struct socket *s = malloc(max*sizeof(struct socket)); 
    for (i=0; i<max; ++i) { 
        s[i].fd = i+1;
        s[i].status = STATUS_INVALID;
        s[i].mask = 0;
        s[i].udata = -1;
        s[i].head = NULL;
        s[i].tail = NULL;
        s[i].slimit = 0;
        s[i].rlimit = 0;
        s[i].sbuffersz = 0;
    }
    s[max-1].fd = -1;
    return s;
}

static struct socket*
_create_socket(struct net *self, socket_t fd, int slimit, int udata, int protocol) {
    assert(fd >= 0);
    if (protocol < LS_PROTOCOL_TCP || protocol > LS_PROTOCOL_IPC) {
        protocol = LS_PROTOCOL_TCP;
    } 
    if (self->free_socket == NULL)
        return NULL;
    struct socket *s = self->free_socket;
    if (s->fd >= 0)
        self->free_socket = &self->sockets[s->fd];
    else
        self->free_socket = NULL;
    s->fd = fd;
    s->protocol = protocol;
    fprintf(stderr, "create socket %d, protocol=%d\n", (int)(s-self->sockets), protocol);
    s->status = STATUS_SUSPEND;
    s->mask = 0; 
    s->udata = udata;
    s->head = NULL;
    s->tail = NULL;
    s->sbuffersz = 0;
    s->rbuffersz = RBUFFER_SZ;
    s->slimit = slimit;
    if (s->slimit <= 0)
        s->slimit = INT_MAX;
    return s;
}

static void
_close_socket(struct net *self, struct socket *s) {
    if (s->fd < 0) return;
    _subscribe(self, s, 0);
    //if (s->status != STATUS_BIND) {
    if (s->fd > STDERR_FILENO) {
        _socket_close(s->fd);
    }
    s->fd = -1;
    s->status = STATUS_INVALID;
    s->udata = 0; 
    while (s->head) {
        struct sbuffer *p = s->head;
        s->head = s->head->next;
        free(p->begin);
        free(p);
    }
    s->tail = NULL;
    s->sbuffersz = 0;
    if (self->free_socket == NULL) {
        self->free_socket = s;
    } else {
        assert(self->tail_socket);
        assert(self->tail_socket->fd == -1);
        self->tail_socket->fd = s-self->sockets;
    }
    self->tail_socket = s;
}

int
socket_close(struct net *self, int id, int force) {
    //fprintf(stderr, "socket_close: %d\n", id);
    struct socket *s = _socket(self, id);
    if (s == NULL) return 0;
    //fprintf(stderr, "socket_close2: %d\n", id);
    if (s->status == STATUS_INVALID)
        return 0;
    if (force || !s->head) {
    //fprintf(stderr, "socket_close3: %d\n", id);
        _close_socket(self, s);
        return 0;
    } else {
        s->status = STATUS_HALFCLOSE;
        return 1;
    }
}

int
socket_enableread(struct net *self, int id, int read) {
    struct socket *s = _socket(self, id);
    if (s == NULL) return 1;
    int mask = 0;
    if (read)
        mask |= NP_RABLE;
    if (s->mask & NP_WABLE)
        mask |= NP_WABLE;
    return _subscribe(self, s, mask);
}

int 
socket_udata(struct net *self, int id, int udata) {
    struct socket *s = _socket(self, id);
    if (s==NULL)
        return 1;
    s->udata = udata;
    return 0;
}

struct net*
net_create(int max) {
    if (max <= 0)
        max = 1;
    struct net *self = malloc(sizeof(struct net));
    if (np_init(&self->np, max)) {
        free(self);
        return NULL;
    }
    self->max = max;
    self->err = 0;
    self->i_events = malloc(max*sizeof(struct np_event));
    self->o_events = malloc(max*sizeof(struct socket_event));
    self->sockets = _alloc_sockets(max);
    self->free_socket = &self->sockets[0];
    self->tail_socket = &self->sockets[max-1];
    return self;
}

void
net_free(struct net *self) {
    if (self == NULL)
        return;

    int i;
    for (i=0; i<self->max; ++i) {
        struct socket *s = &self->sockets[i];
        if (s->status >= STATUS_OPENED) {
            _close_socket(self, s);
        }
    }
    free(self->sockets);
    self->free_socket = NULL;
    self->tail_socket = NULL;
    free(self->i_events);
    free(self->o_events);
    np_fini(&self->np);
    free(self);
}

static int
_read_close(struct socket *s) {
    char buf[1024];
    for (;;) {
        int n = _socket_read(s->fd, buf, sizeof(buf));
        if (n < 0) {
            int err = _socket_geterror(s->fd);
            if (err == SEAGAIN) return 0;
            else if (err == SEINTR) continue;
            else return ERR(err);
        } else if (n == 0) {
            return LS_ERR_EOF;
        } else return 0; // we not care data
    }
}

static int
_read(struct net *self, struct socket *s, void **data) {
    if (s->status == STATUS_HALFCLOSE) {
        self->err = _read_close(s);
        if (self->err) {
            _close_socket(self, s);
            return -1;
        } else return 0;
    }
    int sz = s->rbuffersz;
    void *p = malloc(sz);
    for (;;) {
        int n = _socket_read(s->fd, p, sz);
        //fprintf(stderr, "pid=%d, fd=%d, read :%d, %d\n", getpid(), s->fd, n, (int)(*(char*)p));
        if (n < 0) {
            int err = _socket_geterror(s->fd);
            if (err == SEAGAIN) {
                free(p);
                return 0;
            } else if (err == SEINTR) {
                continue;
            } else {
                free(p);
                _close_socket(self, s);
                self->err = ERR(err);
                return -1;
            }
        } else if (n == 0) {
            // zero indicates end of file
            free(p);
            _close_socket(self, s);
            self->err = LS_ERR_EOF;
            return -1;
        } else {
            if (s->rlimit == 0) {
                if (n == s->rbuffersz)
                    s->rbuffersz <<= 1;
                else if (s->rbuffersz > RBUFFER_SZ && n < (s->rbuffersz<<1))
                    s->rbuffersz >>= 1;
            } 
            *data = p;
            return n;
        } 
    }
}

static inline int
_sendfd(int fd, void *data, int sz, int cfd) {
    char tmp[1] = {0};
    if (data == NULL) {
        data = tmp;
    }
    struct msghdr msg;
    if (cfd < 0) {
        msg.msg_control = NULL;
        msg.msg_controllen = 0;
    } else {
        union {
            struct cmsghdr  cm;
            char            space[CMSG_SPACE(sizeof(int))];
        } cmsg;

        msg.msg_control = (caddr_t)&cmsg;
        msg.msg_controllen = sizeof(cmsg);
        memset(&cmsg, 0, sizeof(cmsg));
        cmsg.cm.cmsg_len = CMSG_LEN(sizeof(int));
        cmsg.cm.cmsg_level = SOL_SOCKET;
        cmsg.cm.cmsg_type = SCM_RIGHTS;
        *(int*)CMSG_DATA(&cmsg.cm) = cfd;
    }
    struct iovec iov[1];
    iov[0].iov_base = data;
    iov[0].iov_len = sz;
    msg.msg_iov = iov;
    msg.msg_iovlen = 1;

    msg.msg_name = NULL;
    msg.msg_namelen = 0;

    msg.msg_flags = 0;
    return sendmsg(fd, &msg, 0);
}

// return -1 for error, or read size (0 no read)
int
socket_readfd(struct net *self, int id, void **data) {
    fprintf(stderr, "socket_readfd:%d\n", id);
    struct socket *s = _socket(self, id);
    if (s == NULL) {
        return -1;
    }
    if (s->protocol != LS_PROTOCOL_IPC) {
        self->err = LS_ERR_STATUS;
        return -1;
    }
    union {
        struct cmsghdr  cm;
        char            space[CMSG_SPACE(sizeof(int))];
    } cmsg;

    struct iovec iov[1];
    iov[0].iov_base = self->recvmsg_buffer;
    iov[0].iov_len = RECVMSG_MAXSIZE;

    struct msghdr msg;
    msg.msg_name = NULL;
    msg.msg_namelen = 0;
    msg.msg_iov = iov;
    msg.msg_iovlen = 1;

    memset(&cmsg, 0, sizeof(cmsg));
    msg.msg_control = (caddr_t)&cmsg;
    msg.msg_controllen = sizeof(cmsg);

    for (;;) {
        int n = recvmsg(s->fd, &msg, 0);
        fprintf(stderr, "recvmsg: fd=%d, n=%d\n", s->fd, n);
        if (n < 0) {
            int err = _socket_geterror(s->fd);
            switch (err) {
            case SEAGAIN: return 0;
            case SEINTR: continue;
            default:
                _close_socket(self, s);
                self->err = ERR(err);
                return -1;
            }
        } else if (n==0) {
            _close_socket(self, s);
            self->err = LS_ERR_EOF;
            return -1;
        } else {
            if (msg.msg_flags & (MSG_TRUNC|MSG_CTRUNC)) {
                _close_socket(self, s);
                self->err = LS_ERR_TRUNC;
                return -1;
            }
            if (cmsg.cm.cmsg_len == CMSG_LEN(sizeof(int))) {
                if (cmsg.cm.cmsg_level != SOL_SOCKET || cmsg.cm.cmsg_type != SCM_RIGHTS) {
                    _close_socket(self, s);
                    self->err = LS_ERR_CMSGTYPE;
                    return -1;
                }
            //fprintf(stderr, "pid=%d, fd=%d -----recvmsg n=%d,=%d, cmsg_len=%d, cfd=%d\n", getpid(),s->fd,n, (int)tmp[0], cmsg.cm.cmsg_len, cfd);
                char *p = malloc(n+sizeof(int));
                *(int *)p = *(int*)CMSG_DATA(&cmsg.cm);
                memcpy(p+4, self->recvmsg_buffer, n);
                return n+sizeof(int);
            } else {
            //fprintf(stderr, "pid=%d, fd=%d -----recvmsg n=%d,=%d, cmsg_len=%d, cfd=%d\n", getpid(),s->fd,n, (int)tmp[0], cmsg.cm.cmsg_len, cfd);
                char *p = malloc(n);
                memcpy(p, self->recvmsg_buffer, n);
                return n;
            }
        }
    }
}

int
socket_read(struct net *self, int id, void **data) {
    struct socket *s = _socket(self, id);
    if (s == NULL) 
        return -1;
    if (s->protocol == LS_PROTOCOL_TCP) {
        return _read(self, s, data);
    } else
        return -1;
}

int
_send_buffer_tcp(struct net *self, struct socket *s) {
    while (s->head) {
        struct sbuffer *b = s->head;
        for (;;) {
            int n = _socket_write(s->fd, b->ptr, b->sz);
            if (n < 0) {
                int err = _socket_geterror(s->fd);
                if (err == SEAGAIN) return 0;
                else if (err == SEINTR) continue;
                else return err;
            } else if (n < b->sz) {
                b->ptr += n;
                b->sz -= n;
                s->sbuffersz -= n;
                return 0;
            } else {
                s->sbuffersz -= n;
                break;
            }
        } 
        s->head = b->next;
        free(b->begin);
        free(b);
    }
    return 0;
}

int
_send_buffer_ipc(struct net *self, struct socket *s) {
    while (s->head) {
        struct sbuffer *b = s->head;
        for (;;) {
            int n = _sendfd(s->fd, b->ptr, b->sz, b->fd);
            if (n < 0) {
                int err = _socket_geterror(s->fd);
                if (err == SEAGAIN) return 0;
                else if (err == SEINTR) continue;
                else return err;
            } else if (n==0) {
                return 0;
            } else if (n<b->sz) {
                b->fd   = -1; // the fd should be send
                b->ptr += n;
                b->sz  -= n;
                s->sbuffersz -= n;
                return 0;
            } else {
                s->sbuffersz -= n;
                break;
            }
        }
        s->head = b->next;
        free(b->begin);
        free(b);
    }
    return 0;
}

int
_send_buffer(struct net *self, struct socket *s) {
    if (s->head == NULL) return 0;
    int err = 0;
    if (s->protocol == LS_PROTOCOL_TCP) {
        err = _send_buffer_tcp(self, s);
    } else if (s->protocol == LS_PROTOCOL_IPC) {
        err = _send_buffer_ipc(self, s);
    }
    if (err == 0) {
        if (s->head == NULL)
            _subscribe(self, s, s->mask & (~NP_WABLE));
    }
    return err;
}

int 
socket_send(struct net* self, int id, void* data, int sz, struct socket_event* event) {
    assert(sz > 0);
    struct socket* s = _socket(self, id);
    if (s == NULL) {
        free(data);
        return -1;
    }
    if (s->protocol != LS_PROTOCOL_TCP || s->status == STATUS_HALFCLOSE) {
        free(data);
        return -1; // do not send
    }
    int err;
    if (s->head == NULL) {
        char *ptr;
        int n = _socket_write(s->fd, data, sz);
        if (n >= sz) {
            free(data);
            return 0;
        } else if (n >= 0) {
            ptr = (char*)data + n;
            sz -= n;
        } else {
            ptr = data;
            err = _socket_geterror(s->fd);
            switch (err) {
            case SEAGAIN: break;
            case SEINTR: break;
            default: goto errout;
            }
        }
        s->sbuffersz += sz;
        if (s->sbuffersz > s->slimit) {
            err = LS_ERR_WBUFOVER;
            goto errout;
        }
        struct sbuffer* p = malloc(sizeof(*p));
        p->next = NULL;
        p->sz = sz;
        p->fd = -1;
        p->begin = data;
        p->ptr = ptr;
        
        s->head = s->tail = p;
        _subscribe(self, s, s->mask|NP_WABLE);
        return 0;
    } else {
        s->sbuffersz += sz;
        if (s->sbuffersz > s->slimit) {
            err = LS_ERR_WBUFOVER;
            goto errout;
        }
        struct sbuffer* p = malloc(sizeof(*p));
        p->next = NULL;
        p->sz = sz;
        p->fd = -1;
        p->begin = data;
        p->ptr = data;
        
        assert(s->tail != NULL);
        assert(s->tail->next == NULL);
        s->tail->next = p;
        s->tail = p;
        return 0;
    }
errout:
    free(data);
    event->id = s-self->sockets;
    event->type = LS_ESOCKERR;
    event->err = ERR(err);
    event->udata = s->udata;
    _close_socket(self, s);
    return 1;
}

int
socket_sendfd(struct net *self, int id, void *data, int sz, int cfd) {
    assert(sz > 0 || (data == NULL && sz == 1)); // if data == NULL, then sz set 1
    struct socket *s = _socket(self, id);
    if (s == NULL) {
        free(data);
        return LS_ERR_NOSOCK;
    }
    if (s->protocol != LS_PROTOCOL_IPC) {
        free(data);
        return LS_ERR_STATUS;
    }
    int err;
    if (s->head == NULL) {
        char *ptr;
        int n = _sendfd(s->fd, data, sz, cfd);
        fprintf(stderr, "socket_sendfd: fd=%d, n=%d, data=%p, sz=%d, cfd=%d\n", s->fd, n, data, sz, cfd);
        if (n >= sz) {
            fprintf(stderr, "send ok\n");
            free(data);
            return 0;
        } else if (n>0) {
            ptr = (char *)data + n;
            sz -= n;
            cfd = -1;
        } else if (n==0) {
            ptr = data;
        } else {
            ptr = data;
            fprintf(stderr, "errno=%d, err=%s\n", errno, strerror(errno));
            err = _socket_geterror(s->fd);
            switch (err) {
            case SEAGAIN: break;
            case SEINTR: break;
            default: goto errout;
            }
        }
        s->sbuffersz += sz;
        if (s->sbuffersz > s->slimit) {
            err = LS_ERR_WBUFOVER;
            goto errout;
        }
        struct sbuffer* p = malloc(sizeof(*p));
        p->next = NULL;
        p->sz = sz;
        p->fd = cfd;
        p->begin = data;
        p->ptr = ptr;
        
        s->head = s->tail = p;
        _subscribe(self, s, s->mask|NP_WABLE);
        return 0;
    } else {
        s->sbuffersz += sz;
        if (s->sbuffersz > s->slimit) {
            err = LS_ERR_WBUFOVER;
            goto errout;
        }
        struct sbuffer* p = malloc(sizeof(*p));
        p->next = NULL;
        p->sz = sz;
        p->fd = cfd;
        p->begin = data;
        p->ptr = data;
        
        assert(s->tail != NULL);
        assert(s->tail->next == NULL);
        s->tail->next = p;
        s->tail = p;
        return 0;
    }
errout:
    free(data);
    _close_socket(self, s);
    return err;
}

int
socket_bind(struct net *self, int fd, int udata, int protocol) {
    struct socket *s;
    s = _create_socket(self, fd, 0, udata, protocol);
    if (s == NULL) {
        self->err = LS_ERR_CREATESOCK;
        return -1;
    }
    if (_socket_nonblocking(fd) == -1) {
        self->err = _socket_error;
        _close_socket(self, s);
        return -1;
    }
    s->status = STATUS_BIND;
    return s-self->sockets;
}

static struct socket *
_accept(struct net *self, struct socket *lis) {
    struct socket *s;
    struct sockaddr_in peer;
    socklen_t l = sizeof(peer);
    socket_t fd = accept(lis->fd, (struct sockaddr*)&peer, &l);
    if (fd < 0) {
        return NULL;
    }
    _socket_keepalive(fd);
    s = _create_socket(self, fd, lis->slimit, lis->udata, LS_PROTOCOL_TCP);
    if (s == NULL) {
        _socket_close(fd);
        return NULL;
    }
    if (_socket_nonblocking(fd) == -1 /*||
        _socket_closeonexec(fd) == -1*/) {
        _close_socket(self, s);
        return NULL;
    }
    s->status = STATUS_CONNECTED;
    return s;
}

int
socket_listen(struct net *self, const char *addr, int port, int udata) {    
    struct addrinfo hints;
    struct addrinfo *result, *rp;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC; // allow IPv4 or IPv6
    hints.ai_socktype = SOCK_STREAM; 
    hints.ai_protocol = IPPROTO_TCP;

    char sport[16];
    snprintf(sport, sizeof(sport), "%u", port);
    if (getaddrinfo(addr, sport, &hints, &result)) {
        self->err = LS_ERR_LISTEN;
        return -1;
    }
    int fd = -1;
    self->err = 0;
    for (rp = result; rp; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd == -1)
            continue;
        if (_socket_nonblocking(fd) == -1 ||
            _socket_closeonexec(fd) == -1 ||
            _socket_reuseaddr(fd)   == -1) {
            self->err = _socket_error;
            _socket_close(fd);
            return -1;
        }
        if (bind(fd, rp->ai_addr, rp->ai_addrlen) == -1) {
            self->err = _socket_error;
            _socket_close(fd);
            fd = -1;
            continue;
        }
        break;
    }
    if (fd == -1) {
        if (self->err == 0)
            self->err = LS_ERR_LISTEN;
        freeaddrinfo(result);
        return -1;
    } 
    self->err = 0;
    freeaddrinfo(result);
    
    if (listen(fd, LISTEN_BACKLOG) == -1) {
        self->err = _socket_error;
        _socket_close(fd);
        return -1;
    }
    struct socket *s;
    s = _create_socket(self, fd, 0, udata, LS_PROTOCOL_TCP);
    if (s == NULL) {
        self->err = LS_ERR_CREATESOCK;
        _socket_close(fd);
        return -1;
    }
    if (_subscribe(self, s, NP_RABLE)) {
        self->err = _socket_error;
        _close_socket(self, s);
        return -1;
    }
    s->status = STATUS_LISTENING;
    return s - self->sockets;
}

static inline int
_onconnect(struct net *self, struct socket *s) {
    int err;
    socklen_t errlen = sizeof(err);
    if (getsockopt(s->fd, SOL_SOCKET, SO_ERROR, (void*)&err, &errlen) == -1) {
        if (err == 0)
            err = _socket_error != 0 ? _socket_error : -1;
    }
    if (err == 0) {
        s->status = STATUS_CONNECTED;
        _subscribe(self, s, 0);
        return 0;
    } else {
        _close_socket(self, s);
        return err;
    }
}

int
socket_connect(struct net *self, const char *addr, int port, int block, int udata) {
    self->err = 0;
    struct addrinfo hints;
    struct addrinfo *result, *rp;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC; // allow IPv4 or IPv6
    hints.ai_socktype = SOCK_STREAM; 
    hints.ai_protocol = IPPROTO_TCP;

    char sport[16];
    snprintf(sport, sizeof(sport), "%u", port);
    if (getaddrinfo(addr, sport, &hints, &result)) {
        self->err = LS_ERR_CONNECT;
        return -1;
    }
    int fd = -1, status;
    self->err = 0;
    for (rp = result; rp != NULL; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd == -1)
            continue;
        if (_socket_keepalive(fd) != 0) {
            self->err = _socket_error;
            _socket_close(fd);
            return -1;
        }
        if (!block)
            if (_socket_nonblocking(fd) == -1) {
                self->err = _socket_error;
                _socket_close(fd);
                return -1;
            }
        if (connect(fd, rp->ai_addr, rp->ai_addrlen) == -1) {
            if (block) {
                self->err = _socket_error; 
                _socket_close(fd);
                fd = -1;
                continue;
            } else {
                int err = _socket_geterror(fd);
                if (!SECONNECTING(err)) {
                    self->err = err;
                    _socket_close(fd);
                    fd = -1;
                    continue;
                }
            }
            status = STATUS_CONNECTING;
        } else {
            status = STATUS_CONNECTED;
        }
        if (block)
            if (_socket_nonblocking(fd) == -1) { // 仅connect阻塞
                self->err = _socket_error;
                return -1;
            }
        break;
    }
    if (fd == -1) {
        if (self->err == 0)
            self->err = LS_ERR_CONNECT;
        freeaddrinfo(result);
        return -1;
    } 
    self->err = 0;
    freeaddrinfo(result);

    struct socket *s;
    s = _create_socket(self, fd, 0, udata, LS_PROTOCOL_TCP);
    if (s == NULL) {
        self->err = LS_ERR_CREATESOCK;
        _socket_close(fd);
        return -1;
    }
    s->status = status;
    if (s->status == STATUS_CONNECTING) {
        if (_subscribe(self, s, NP_RABLE|NP_WABLE)) {
            self->err = _socket_error; 
            _close_socket(self, s);
            return -1;
        } 
        self->err = LS_CONNECTING;
    }
    return s - self->sockets;
}

int
socket_poll(struct net *self, int timeout, struct socket_event **events) {
    struct socket_event *oe = self->o_events;
    int n = np_poll(&self->np, self->i_events, self->max, timeout);
    int i;
    for (i=0; i<n; ++i) {
        struct np_event *ie = &self->i_events[i];
        struct socket *s = ie->ud;
        
        switch (s->status) {
        case STATUS_LISTENING: {
            struct socket *lis = s;
            s = _accept(self, lis);
            if (s) {
                oe->type = LS_EACCEPT;
                oe->id = s-self->sockets; 
                oe->udata = s->udata;
                oe->listenid = lis-self->sockets;
                oe++;
            }} break;
        case STATUS_CONNECTING:
            oe->id = s-self->sockets;
            oe->udata = s->udata;
            oe->err = _onconnect(self, s);
            if (oe->err) oe->type = LS_ECONNERR;
            else if (ie->read) oe->type = LS_ECONN_THEN_READ;
            else oe->type = LS_ECONNECT;
            oe++;
            break;
        case STATUS_INVALID:
            break;
        default: 
            if (ie->write) {
                int err = _send_buffer(self, s);
                if (err) {
                    oe->type = LS_ESOCKERR; 
                    oe->id = s-self->sockets;
                    oe->udata = s->udata;
                    oe->err = err;
                    oe++;
                    _close_socket(self, s);
                    break;
                }
                if (s->status == STATUS_HALFCLOSE &&
                    s->head == NULL) {
                    oe->type = LS_EWRIDONECLOSE;
                    oe->id = s-self->sockets;
                    oe->udata = s->udata;
                    oe++;
                    _close_socket(self, s);
                    break;
                }
            }
            if (ie->read) {
                oe->id = s-self->sockets;
                oe->udata = s->udata;
                oe->type = LS_EREAD;
                oe->err  = s->protocol;
                oe++;
            }
            break;
        }
    }
    *events = self->o_events;
    return oe - self->o_events;
}

int 
socket_address(struct net *self, int id, struct socket_addr *addr) {
    struct socket *s = _socket(self, id);
    if (s == NULL) return 1;
    struct sockaddr_in peer;
    socklen_t l = sizeof(peer);
    if (!getpeername(s->fd, (struct sockaddr *)&peer, &l)) {
        inet_ntop(AF_INET, &peer.sin_addr, addr->ip, sizeof(addr->ip));
        addr->port = ntohs(peer.sin_port);
        return 0;
    } else
        return 1;
}

int
socket_limit(struct net *self, int id, int slimit, int rlimit) {
    struct socket *s = _socket(self, id);
    if (s == NULL) return 1;
    if (slimit <= 0) {
        s->slimit = INT_MAX;
    } else {
        s->slimit = slimit;
    }
    if (rlimit <= 0) {
        s->rlimit = 0;
    } else {
        s->rlimit = rlimit;
        s->rbuffersz = rlimit;
    }
    return 0;
}

const char *
socket_error(struct net *self, int err) {
    if (err <= 0) {
        err= -err;
        if (err<0 || err>=sizeof(STRERROR)/sizeof(STRERROR[0]))
            err=0;
        return STRERROR[err];
    } else {
        return _socket_strerror(err);
    }
}

int
socket_lasterrno(struct net *self) {
    return self->err;
}

int 
socket_fd(struct net *self, int id) {
    struct socket *s = _socket(self, id);
    if (s) return s->fd;
    return SOCKET_INVALID;
}
