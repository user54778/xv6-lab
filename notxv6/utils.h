#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <assert.h>
#include <pthread.h>
#include <sys/time.h>
#include <string.h>

// Wrappers
int Pthread_mutex_init(pthread_mutex_t *, const pthread_mutexattr_t *); 
int Pthread_mutex_lock(pthread_mutex_t *);
int Pthread_mutex_unlock(pthread_mutex_t *);
int Pthread_mutex_destroy(pthread_mutex_t *);
int Pthread_cond_wait(pthread_cond_t *, pthread_mutex_t *);
int Pthread_cond_broadcast(pthread_cond_t *);

void init_locks(); 
void destroy_locks();

