#ifndef THREADS_SIGNAL_H
#define THREADS_SIGNAL_H

#include <debug.h>
#include <list.h>
#include <stdint.h>

struct thread;

// SIG_KILL 4
// SIG_USER 2
// SIG_CPU 3
// SIG_CHLD 1
// SIG_UBLOCK 0

enum sighandler_t {
    SIG_DFL,
    SIG_IGN
};
struct signal_data {
	int signum;
	int sender;
	struct list_elem threadelem;
};

enum sighandler_t Signal(int signum, enum sighandler_t handler);
int kill(int pid, int sig);

int sigprocmask(int how, const unsigned short *set, unsigned short *oldset);

int sigemptyset(unsigned short *set);
int sigfillset(unsigned short *set);
int sigaddset(unsigned short *set, int signum);
int sigdelset(unsigned short *set, int signum);

void handler(int sender, int signum);

#endif