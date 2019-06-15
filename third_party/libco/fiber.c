/*
  libco.win (2008-01-28)
  authors: Nach, byuu
  license: public domain
*/

#include "libco.h"

#define WINVER 0x0400
#define _WIN32_WINNT 0x0400
#include <windows.h>

#if defined(_MSC_VER)
__declspec(thread) static cothread_t co_active_ = 0;
#elif defined(__GNUC__)
static __thread cothread_t co_active_ = 0;
#else
#error "Unknown compiler."
#endif

struct fiber_data
{
  void (*coentry)(void *);
  void *userdata;
};

static void __stdcall co_thunk(void *coentry) {
  struct fiber_data data = *(const struct fiber_data*)coentry;
  free(coentry);
  data.coentry(data.userdata);
}

cothread_t co_active() {
  if(!co_active_) {
    ConvertThreadToFiber(0);
    co_active_ = GetCurrentFiber();
  }
  return co_active_;
}

cothread_t co_create(unsigned int heapsize, void (*coentry)(void *), void *userdata) {
  struct fiber_data *data;
  if(!co_active_) {
    ConvertThreadToFiber(0);
    co_active_ = GetCurrentFiber();
  }
  data = (struct fiber_data*)malloc(sizeof(struct fiber_data));
  data->coentry = coentry;
  data->userdata = userdata;
  return (cothread_t)CreateFiber(heapsize, co_thunk, data);
}

void co_delete(cothread_t cothread) {
  DeleteFiber(cothread);
}

void co_switch(cothread_t cothread) {
  co_active_ = cothread;
  SwitchToFiber(cothread);
}

