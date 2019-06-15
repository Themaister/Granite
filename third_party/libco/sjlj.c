/*
  libco.sjlj (2008-01-28)
  author: Nach
  license: public domain
*/

/*
 * Note this was designed for UNIX systems. Based on ideas expressed in a paper
 * by Ralf Engelschall.
 * For SJLJ on other systems, one would want to rewrite springboard() and
 * co_create() and hack the jmb_buf stack pointer.
 */

#include "libco.h"
#include <stdlib.h>
#include <signal.h>
#include <setjmp.h>
#include <pthread.h>

typedef struct {
  sigjmp_buf context;
  void (*coentry)(void *);
  void *stack;
  void *userdata;
} cothread_struct;

static __thread cothread_struct co_primary;
static __thread cothread_struct *creating, *co_running = 0;

static pthread_mutex_t global_lock = PTHREAD_MUTEX_INITIALIZER;

static void springboard(int ignored) {
  if(sigsetjmp(creating->context, 0)) {
    co_running->coentry(co_running->userdata);
  }
}

cothread_t co_active() {
  if(!co_running) co_running = &co_primary;
  return (cothread_t)co_running;
}

cothread_t co_create(unsigned int size, void (*coentry)(void *), void *userdata) {
  if(!co_running) co_running = &co_primary;

  cothread_struct *thread = (cothread_struct*)malloc(sizeof(cothread_struct));
  if(thread) {
    struct sigaction handler;
    struct sigaction old_handler;

    stack_t stack;
    stack_t old_stack;

    thread->coentry = thread->stack = 0;
    thread->userdata = userdata;

    stack.ss_flags = 0;
    stack.ss_size = size;
    thread->stack = stack.ss_sp = malloc(size);
    if(stack.ss_sp && !sigaltstack(&stack, &old_stack)) {
      handler.sa_handler = springboard;
      handler.sa_flags = SA_ONSTACK;
      sigemptyset(&handler.sa_mask);
      creating = thread;

      /* Signal state is global. Need locking if we're using cothreads from multiple threads. */
      pthread_mutex_lock(&global_lock);
      if(!sigaction(SIGUSR1, &handler, &old_handler)) {
        if(!pthread_kill(pthread_self(), SIGUSR1)) {
          thread->coentry = coentry;
        }
        sigaltstack(&old_stack, 0);
        sigaction(SIGUSR1, &old_handler, 0);
      }
      pthread_mutex_unlock(&global_lock);
    }

    if(thread->coentry != coentry) {
      co_delete(thread);
      thread = 0;
    }
  }

  return (cothread_t)thread;
}

void co_delete(cothread_t cothread) {
  if(cothread) {
    if(((cothread_struct*)cothread)->stack) {
      free(((cothread_struct*)cothread)->stack);
    }
    free(cothread);
  }
}

void co_switch(cothread_t cothread) {
  if(!sigsetjmp(co_running->context, 0)) {
    co_running = (cothread_struct*)cothread;
    siglongjmp(co_running->context, 1);
  }
}

