#ifndef __socket_h__
#define __socket_h__

#include <stdint.h>
#include "socket_define.h"

struct net;
struct socket_event;
struct socket_addr;

struct net *net_create(int max);
void net_free(struct net *self);

int socket_bind(struct net *self, int fd, int udata, int protocol);
int socket_listen(struct net *self, const char *addr, int port, int udata);
int socket_connect(struct net *self, const char *addr, int port, int block, int udata);
int socket_udata(struct net *self, int id, int udata);
int socket_close(struct net *self, int id, int force);
int socket_enableread(struct net *self, int id, int read);
int socket_poll(struct net *self, int timeout, struct socket_event **events);
int socket_send(struct net *self, int id, void *data, int sz, struct socket_event *event);
int socket_read(struct net *self, int id, void **data);
int socket_sendfd(struct net *self, int id, void *data, int sz, int fd);
int socket_readfd(struct net *self, int id, void **data);
int socket_address(struct net *self, int id, struct socket_addr *addr);
int socket_limit(struct net *self, int id, int slimit, int rlimit);
int socket_lasterrno(struct net *self);
const char *socket_error(struct net *self, int err);
int socket_fd(struct net *self, int id);

#endif
