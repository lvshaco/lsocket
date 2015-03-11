#ifndef __NET_H__ 
#define __NET_H__

#include <stdint.h>
#include "net_define.h"

struct net;
struct net_event;
struct net_addr;

struct net *net_create(int max);
void net_free(struct net *self);

int net_listen(struct net *self, const char *addr, int port, int udata);
int net_connect(struct net *self, const char *addr, int port, int block, int udata);
int net_close(struct net *self, int id, int force);
int net_subscribe(struct net *self, int id, int read);
int net_poll(struct net *self, int timeout, struct net_event **events);
int net_send(struct net *self, int id, void *data, int sz, struct net_event *event);
int net_read(struct net *self, int id, void **data);
int net_address(struct net *self, int id, struct net_addr *addr);
int net_slimit(struct net *self, int id, int slimit);
int net_lasterrno(struct net *self);
const char *net_error(struct net *self, int err);

#endif
