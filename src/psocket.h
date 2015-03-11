#ifndef __psocket_h__
#define __psocket_h__

#include <stdint.h>
#include "socket_define.h"

typedef void (*psocket_dispatch)(struct socket_event *event);

int psocket_init(int cmax, psocket_dispatch f);
void psocket_fini();
int psocket_listen(const char *addr, int port);
int psocket_connect(const char *addr, int port);
int psocket_close(int id, int force);
int psocket_subscribe(int id, int read);
int psocket_poll(int timeout);
int psocket_send(int id, void *data, int sz);
int psocket_read(int id, void **data);
int psocket_address(int id, struct socket_addr *addr);
int psocket_slimit(int id, int slimit);
int psocket_lasterrno();
const char *psocket_error(int err);
#define PSOCKET_ERR psocket_error(psocket_lasterrno())

#endif
