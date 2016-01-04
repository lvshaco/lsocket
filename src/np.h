#ifndef __np_h__
#define __np_h__

#include <stdbool.h>

#define NP_RABLE 1
#define NP_WABLE 2

struct np_event {
    void* ud;
    bool read;
    bool write;
};

struct np_state;
static int np_init(struct np_state* np, int max);
static void np_fini(struct np_state* np);
static int np_add(struct np_state* np, int fd, int mask, void* ud);
static int np_mod(struct np_state* np, int fd, int mask, void* ud); 
static int np_del(struct np_state* np, int fd); 
static int np_poll(struct np_state* np, struct np_event* e, int max, int timeout);
    
#ifdef __linux__
#include "np_epoll.h"
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined (__NetBSD__)
#include "np_kqueue.h"
#else
#include "np_select.h"
#endif


#endif
