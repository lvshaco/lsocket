#ifndef __np_kqueue_h__
#define __np_kqueue_h__

#include <sys/event.h>
#include <sys/types.h>
#include <sys/times.h>

struct np_state {
    int kqueue_fd;
    struct kevent* ev;
};

static int
np_init(struct np_state* np, int max) {
    int kqueue_fd = kqueue();
    if (kqueue_fd == -1)
        return 1;
    int flag = fcntl(kqueue_fd, F_GETFD, 0);
    if (flag == -1)
        return 1;
    if (fcntl(kqueue_fd, F_SETFD, flag | FD_CLOEXEC))
        return 1;
    np->kqueue_fd = kqueue_fd;
    np->ev = malloc(sizeof(struct kevent) * max);
    return 0;
}

static void
np_fini(struct np_state* np) {
    if (np->ev) {
        free(np->ev);
        np->ev = NULL;
    }
    if (np->kqueue_fd != -1) {
        close(np->kqueue_fd);
        np->kqueue_fd = -1;
    }
}

static int
np_del(struct np_state* np, int fd) {
    struct kevent ke;
	EV_SET(&ke, fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
	kevent(np->kqueue_fd, &ke, 1, NULL, 0, NULL);
	EV_SET(&ke, fd, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
	kevent(np->kqueue_fd, &ke, 1, NULL, 0, NULL);
    return 0;
}

static int
np_add(struct np_state* np, int fd, int mask, void* ud) {
    struct kevent ke;
	EV_SET(&ke, fd, EVFILT_READ, EV_ADD, 0, 0, ud);
	if (kevent(np->kqueue_fd, &ke, 1, NULL, 0, NULL) == -1) {
        return -1;
    }
	EV_SET(&ke, fd, EVFILT_WRITE, EV_ADD, 0, 0, ud);
	if (kevent(np->kqueue_fd, &ke, 1, NULL, 0, NULL) == -1) {
        EV_SET(&ke, fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
        kevent(np->kqueue_fd, &ke, 1, NULL, 0, NULL);
        return -1;
    }
    if (!(mask & NP_WABLE)) {
        EV_SET(&ke, fd, EVFILT_WRITE, EV_DISABLE, 0, 0, ud);
        if (kevent(np->kqueue_fd, &ke, 1, NULL, 0, NULL) == -1) {
            np_del(np, fd);
            return -1;
        }
    }
    if (!(mask & NP_RABLE)) {
        EV_SET(&ke, fd, EVFILT_READ, EV_DISABLE, 0, 0, ud);
        if (kevent(np->kqueue_fd, &ke, 1, NULL, 0, NULL) == -1) {
            np_del(np, fd);
            return -1;
        }
    }
    return 0;
}

static int
np_mod(struct np_state* np, int fd, int mask, void* ud) {
    struct kevent ke;
    if (!(mask & NP_WABLE)) {
        EV_SET(&ke, fd, EVFILT_WRITE, EV_DISABLE, 0, 0, ud);
        return kevent(np->kqueue_fd, &ke, 1, NULL, 0, NULL);
    } else if (!(mask & NP_RABLE)) {
        EV_SET(&ke, fd, EVFILT_READ, EV_DISABLE, 0, 0, ud);
        return kevent(np->kqueue_fd, &ke, 1, NULL, 0, NULL);
    } else {
        return -1;
    }
}

static int
np_poll(struct np_state* np, struct np_event* e, int max, int timeout) {
    struct timespec tv;
    struct timespec *ptv;
    if (timeout >= 0) {
        tv.tv_sec = timeout/1000;
        tv.tv_nsec = timeout%1000*1000000;
        ptv = &tv;
    } else {
        ptv = NULL;
    }
    struct kevent* ev = np->ev;
    int i;
    int n = kevent(np->kqueue_fd, NULL, 0, ev, max, ptv);
    for (i=0; i<n; ++i) {
        e[i].ud    = ev[i].udata;
        e[i].read  = ev[i].filter == EVFILT_READ;
        e[i].write = ev[i].filter == EVFILT_WRITE;
    }
    return n;
}

#endif
