#ifndef __cnet_h__
#define __cnet_h__

#include <stdint.h>
#include "net_define.h"

typedef void (*cnet_dispatch)(struct net_event *event);

int cnet_init(int cmax, cnet_dispatch f);
void cnet_fini();
int cnet_listen(const char *addr, int port);
int cnet_connect(const char *addr, int port);
int cnet_close(int id, int force);
int cnet_subscribe(int id, int read);
int cnet_poll(int timeout);
int cnet_send(int id, void *data, int sz);
int cnet_read(int id, void **data);
int cnet_address(int id, struct net_addr *addr);
int cnet_slimit(int id, int slimit);
int cnet_lasterrno();
const char *cnet_error(int err);
#define SH_NETERR cnet_error(cnet_lasterrno())


#endif
