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

#define LISTEN_BACKLOG 511
#define RBUFFER_SZ 64

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
};

struct sbuffer {
    struct sbuffer *next;
    int sz;
    char *begin;
    char *ptr;
};

struct socket {
    socket_t fd;
    int status;
    int mask;
    int udata;
    struct sbuffer *head;
    struct sbuffer *tail; 
    int sbuffersz;
    int rbuffersz;
    int slimit; 
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
        s[i].sbuffersz = 0;
    }
    s[max-1].fd = -1;
    return s;
}

static struct socket*
_create_socket(struct net *self, socket_t fd, int slimit, int udata) {
    assert(fd >= 0);
    if (self->free_socket == NULL)
        return NULL;
    struct socket *s = self->free_socket;
    if (s->fd >= 0)
        self->free_socket = &self->sockets[s->fd];
    else
        self->free_socket = NULL;
    s->fd = fd;
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
    _socket_close(s->fd);
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
    struct socket *s = _socket(self, id);
    if (s == NULL) return 0;
    if (s->status == STATUS_INVALID)
        return 0;
    if (force || !s->head) {
        _close_socket(self, s);
        return 0;
    } else {
        s->status = STATUS_HALFCLOSE;
        return 1;
    }
}

int
socket_subscribe(struct net *self, int id, int read) {
    struct socket *s = _socket(self, id);
    if (s == NULL) return 1;
    int mask = 0;
    if (read)
        mask |= NP_RABLE;
    if (s->mask & NP_WABLE)
        mask |= NP_WABLE;
    return _subscribe(self, s, mask);
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

int
socket_read(struct net *self, int id, void **data) {
    struct socket *s = _socket(self, id);
    if (s == NULL) return -1;
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
        if (n < 0) {
            int e = _socket_geterror(s->fd);
            if (e == SEAGAIN) {
                free(p);
                return 0;
            } else if (e == SEINTR) {
                continue;
            } else {
                free(p);
                _close_socket(self, s);
                self->err = ERR(e);
                return -1;
            }
        } else if (n == 0) {
            // zero indicates end of file
            free(p);
            _close_socket(self, s);
            self->err = LS_ERR_EOF;
            return -1;
        } else {
            //if (n == s->rbuffersz)
                //s->rbuffersz <<= 1;
            //else if (s->rbuffersz > RBUFFER_SZ && n < (s->rbuffersz<<1))
                //s->rbuffersz >>= 1;
            *data = p;
            return n;
        } 
    }
}

int
_send_buffer(struct net *self, struct socket *s) {
    if (s->head == NULL) return 0;
    while (s->head) {
        struct sbuffer *p = s->head;
        for (;;) {
            int n = _socket_write(s->fd, p->ptr, p->sz);
            if (n < 0) {
                int e = _socket_geterror(s->fd);
                if (e == SEAGAIN) return 0;
                else if (e == SEINTR) continue;
                else return e;
            } else if (n == 0) { 
                return 0;
            } else if (n < p->sz) {
                p->ptr += n;
                p->sz -= n;
                s->sbuffersz -= n;
                return 0;
            } else {
                s->sbuffersz -= n;
                break;
            }
        }
        s->head = p->next;
        free(p->begin);
        free(p);
    }
    if (s->head == NULL)
        _subscribe(self, s, s->mask & (~NP_WABLE));
    return 0;
}

int 
socket_send(struct net* self, int id, void* data, int sz, struct socket_event* event) {
    assert(sz > 0);
    struct socket* s = _socket(self, id);
    if (s == NULL) {
        free(data);
        return -1;
    }
    if (s->status == STATUS_HALFCLOSE) {
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

static struct socket *
_accept(struct net *self, struct socket *lis) {
    struct socket *s;
    struct sockaddr_in peer;
    socklen_t l = sizeof(peer);
    socket_t fd = accept(lis->fd, (struct sockaddr*)&peer, &l);
    if (fd < 0)
        return NULL;
    s = _create_socket(self, fd, lis->slimit, lis->udata);
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
    s = _create_socket(self, fd, 0, udata);
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
        if (!block)
            if (_socket_nonblocking(fd) == -1) {
                self->err = _socket_error;
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
    s = _create_socket(self, fd, 0, udata);
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
    struct socket_event *oe = self->o_events;;
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
            if (ie->write) {
                oe->id = s-self->sockets;
                oe->udata = s->udata;
                oe->err = _onconnect(self, s);
                if (oe->err) oe->type = LS_ECONNERR;
                else if (ie->read) oe->type = LS_ECONN_THEN_READ;
                else oe->type = LS_ECONNECT;
                oe++;
            }
            break;
        case STATUS_CONNECTED:
        case STATUS_HALFCLOSE:
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
socket_slimit(struct net *self, int id, int slimit) {
    struct socket *s = _socket(self, id);
    if (s == NULL) return 1;
    if (slimit < 0) slimit = 0;
    s->slimit = slimit;
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
