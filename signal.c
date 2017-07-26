#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/switch.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif

enum sighandler_t Signal(int signum, enum sighandler_t handler) {
	if (signum == 4) return 0;

	ASSERT (intr_get_level () == INTR_ON);
	
	enum intr_level old_level;
	old_level = intr_disable ();
	
	struct thread * cur = thread_current();
	unsigned short k = cur->mask;
	int temp = signum;
	while(temp > 0){
		k /= 2;
		temp--;
	}
	enum sighandler_t old_handler = (k&1)?SIG_IGN : SIG_DFL;
	
	k = 1;
	temp = signum;

	if (old_handler != handler) {
		// k = 1 << signum;
		while(temp > 0){
			k *= 2;
			temp--;
		}
		cur->mask ^= k;
	}

	intr_set_level (old_level);
	return old_handler;
}

int kill(int tid, int sig) {
	printf("a\n");
	if (sig == 1)return -1;
	// if(sig == 3)return -1;

	if(tid < 3){
		printf("Invalid Thread\n");
		return -1;
	}
	printf("b\n");
	ASSERT (intr_get_level () == INTR_ON);
	
	enum intr_level old_level;
	old_level = intr_disable ();

	struct thread * x = get_thread(tid);
	unsigned short k;

	if (x == NULL) {
		intr_set_level (old_level); 
		return -1;
	}
	printf("c\n");

	k = x->mask;
	printf("Signal: %d,Mask: %d\n",sig,k);
	int temp = sig;
	while(temp > 0){
		k /= 2;
		temp--;
	}
	printf("K: %d\n",k);
	if (sig != 4){
		if(k%2) {
			printf("SIGNAl Ignored\n");
			intr_set_level (old_level);
			return 0;
		}
	}
	printf("d\n");

	if (sig == 0 && x->status == THREAD_BLOCKED) {
		// if (x->status == THREAD_BLOCKED) {
		list_push_back(&to_unblock_list, &x->block_elem);
	}
	if(sig == 0){
		intr_set_level (old_level);
		return 0;
	}

	if (sig == 4 && x->parent_tid != running_thread()->tid) {
		printf("Kill recv\n");
		intr_set_level(old_level);
		return -1;
	}

	int signal_type = x->signals[sig].signum;
	struct thread *cur;
	cur = running_thread();
	x->signals[sig].sender = cur->tid;

	if (signal_type == -1) {
		x->signals[sig].signum = sig;
		list_push_back(&x->signals_queue, &x->signals[sig].threadelem);
		intr_set_level (old_level);
		return 0;
	}

	// x->signals[sig].sender = cur->tid;
	intr_set_level (old_level);
	return 0;
}

// 0 - SIGBLOCK 1 - SIG_UNBLOCK 2 - SIG_SETMASK
int sigprocmask(int how, const unsigned short *set, unsigned short *oldset){

	if (*set >= 16) return -1;
	
	ASSERT (intr_get_level () == INTR_ON);
	
	enum intr_level old_level;
	old_level = intr_disable ();
	
	struct thread * cur = running_thread();
	
	if (oldset) *oldset = cur->mask;

	if (set == NULL) {
		intr_set_level (old_level);
		return 0;	
	}
	
	if(how == 0) {
		cur->mask |= (*set);
	}
	else if(how == 1) {
		cur->mask &= ( ((unsigned short)15) ^ (*set) );
	}
	else if(how == 2) {
		cur->mask = (*set);
	}
	else {
		intr_set_level (old_level);
		return -1;
	}
	intr_set_level (old_level);
	return 0;
}

int sigemptyset(unsigned short *set){
	if (set == NULL) {
		return -1;
	}
	ASSERT (intr_get_level () == INTR_ON);
	enum intr_level old_level;
	old_level = intr_disable ();
	*set = 0;
	intr_set_level (old_level);
	return 0;
}

int sigfillset(unsigned short *set){
	if (set == NULL) {
		return -1;
	}
	ASSERT (intr_get_level () == INTR_ON);
	enum intr_level old_level;
	old_level = intr_disable ();
	*set = (unsigned short)15;
	intr_set_level (old_level);
	return 0;
}

int sigaddset(unsigned short *set, int signum){
	if (signum >= 4) {
		return -1;
	}
	if(set == NULL){
		return -1;
	}
	ASSERT (intr_get_level () == INTR_ON);
	enum intr_level old_level;
	old_level = intr_disable ();
	unsigned short k = 1;
	int temp = signum;
	while(temp > 0){
		k *= 2;
		temp--;
	}
	*set |= k;
	intr_set_level (old_level);
	return 0;
}

int sigdelset(unsigned short *set, int signum){
	if(signum >= 4){
		return -1;
	}

	if(set == NULL){
		return -1;
	}

	ASSERT (intr_get_level () == INTR_ON);
	
	enum intr_level old_level;
	unsigned short k = 1;

	old_level = intr_disable ();
	// k = ((unsigned short)1) << signum;
	int temp = signum;
	while(temp > 0){
		k *= 2;
		temp--;
	}
	*set = (*set) & (~k);
	intr_set_level (old_level);
	return 0;
}

void handler(int sender, int signum){
	if(signum == 4){
		// SIG_KILL
		printf("SIGKILL: %d Killed by %d\n", running_thread()->tid, sender);
		thread_exit();
	}else if(signum == 2){
		// SIG_USER 
		printf("SIG_USER: %d sent SIG_USER to %d\n", sender, running_thread()->tid);
	}else if(signum == 3){
		// CPU
		printf("SIG_CPU: Lifetime of %d = %lld\n", running_thread()->tid, running_thread()->lifetime);
		thread_exit();
	}else if(signum == 1){
		// SIG_CHLD
		running_thread()->child_alive--;
		printf("SIG_CHLD: Thread %d: %d Children, %d alive\n", running_thread()->tid, running_thread()->child_total, running_thread()->child_alive);
	}
}