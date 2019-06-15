/*
  libco
  version: 0.17 (2015-06-18)
  author: byuu
  license: public domain
*/

// Modified slightly for Granite's purposes.

#ifndef LIBCO_H
#define LIBCO_H

#ifdef __cplusplus
extern "C" {
#endif

typedef void* cothread_t;

cothread_t co_active();
cothread_t co_create(unsigned int, void (*)(void *), void *userdata);
void co_delete(cothread_t);
void co_switch(cothread_t);

#ifdef __cplusplus
}
#endif

/* ifndef LIBCO_H */
#endif
