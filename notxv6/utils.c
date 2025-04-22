#include "utils.h"

int Pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr) {
  int rc;
  if ((rc = pthread_mutex_init(mutex, attr)) != 0) {
    fprintf(stderr, "pthread_mutex_init failed %s\n", strerror(rc));
    return rc;
  }
  return 0;
}

int Pthread_mutex_lock(pthread_mutex_t *mutex) {
  int rc;
  if ((rc = pthread_mutex_lock(mutex)) != 0) {
    fprintf(stderr, "pthread_mutex_lock failed %s\n", strerror(rc));
    return rc;
  }
  return 0;
}


int Pthread_mutex_unlock(pthread_mutex_t *mutex) {
  int rc;
  if ((rc = pthread_mutex_unlock(mutex)) != 0) {
    fprintf(stderr, "pthread_mutex_unlock failed %s\n", strerror(rc));
    return rc;
  }
  return 0;
}

int Pthread_mutex_destroy(pthread_mutex_t *mutex) {
  int rc;
  if ((rc = pthread_mutex_destroy(mutex)) != 0) {
    fprintf(stderr, "pthread_mutex_destroy failed %s\n", strerror(rc));
    return rc;
  }
  return 0;
}

// NOTE: You MUST call with the mutex locked by calling thread to avoid UB.
int Pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex) {
  int rc;
  if ((rc = pthread_cond_wait(cond, mutex)) != 0) {
    fprintf(stderr, "pthread_cond_wait failed %s\n", strerror(rc));
    return rc;
  }
  return 0;
}

int Pthread_cond_broadcast(pthread_cond_t *cond) {
  int rc;
  if ((rc = pthread_cond_broadcast(cond)) != 0) {
    fprintf(stderr, "pthread_cond_broadcast failed %s\n", strerror(rc));
    return rc;
  }
  return 0;
}
