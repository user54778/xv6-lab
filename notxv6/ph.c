#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <assert.h>
#include <pthread.h>
#include <sys/time.h>
#include <string.h>

#define NBUCKET 5
#define NKEYS 100000

// Wrappers
int Pthread_mutex_init(pthread_mutex_t *, const pthread_mutexattr_t *); 
int Pthread_mutex_lock(pthread_mutex_t *);
int Pthread_mutex_unlock(pthread_mutex_t *);
int Pthread_mutex_destroy(pthread_mutex_t *);

struct entry {
  int key;
  int value;
  struct entry *next;
};
struct entry *table[NBUCKET];
int keys[NKEYS];
int nthread = 1;

// Create a mutex for each bucket
pthread_mutex_t mutexes[NBUCKET];

void
init_locks() {
  for (int i = 0; i < NBUCKET; i++) {
    Pthread_mutex_init(&mutexes[i], NULL);
  }
}

void
destroy_locks() {
  for (int i = 0; i < NBUCKET; i++) {
    Pthread_mutex_destroy(&mutexes[i]);
  }
}

double
now()
{
 struct timeval tv;
 gettimeofday(&tv, 0);
 return tv.tv_sec + tv.tv_usec / 1000000.0;
}

static void 
insert(int key, int value, struct entry **p, struct entry *n)
{
  struct entry *e = malloc(sizeof(struct entry));
  e->key = key;
  e->value = value;
  e->next = n;
  *p = e;
}

static 
void put(int key, int value)
{

  int i = key % NBUCKET;

  // is the key already present?
  Pthread_mutex_lock(&mutexes[i]);
  struct entry *e = 0;
  for (e = table[i]; e != 0; e = e->next) {
    if (e->key == key)
      break;
  }
  if(e){
    // update the existing key.
    e->value = value;
  } else {
    // the new is new.
    insert(key, value, &table[i], table[i]);
  }
  Pthread_mutex_unlock(&mutexes[i]);
}

static struct entry*
get(int key)
{
  int i = key % NBUCKET;
  Pthread_mutex_lock(&mutexes[i]);
  struct entry *e = 0;
  for (e = table[i]; e != 0; e = e->next) {
    if (e->key == key) {
      break;
    }
  }
  Pthread_mutex_unlock(&mutexes[i]);
  return e;
}

static void *
put_thread(void *xa)
{
  int n = (int) (long) xa; // thread number
  int b = NKEYS/nthread;

  for (int i = 0; i < b; i++) {
    put(keys[b*n + i], n);
  }

  return NULL;
}

static void *
get_thread(void *xa)
{
  int n = (int) (long) xa; // thread number
  int missing = 0;

  for (int i = 0; i < NKEYS; i++) {
    struct entry *e = get(keys[i]);
    if (e == 0) missing++;
  }
  printf("%d: %d keys missing\n", n, missing);
  return NULL;
}

int
main(int argc, char *argv[])
{
  pthread_t *tha;
  void *value;
  double t1, t0;

  if (argc < 2) {
    fprintf(stderr, "Usage: %s nthreads\n", argv[0]);
    exit(-1);
  }
  nthread = atoi(argv[1]);
  tha = malloc(sizeof(pthread_t) * nthread);
  srandom(0);
  assert(NKEYS % nthread == 0);
  for (int i = 0; i < NKEYS; i++) {
    keys[i] = random();
  }
  
  init_locks();

  //
  // first the puts
  //
  t0 = now();
  for(int i = 0; i < nthread; i++) {
    assert(pthread_create(&tha[i], NULL, put_thread, (void *) (long) i) == 0);
  }
  for(int i = 0; i < nthread; i++) {
    assert(pthread_join(tha[i], &value) == 0);
  }
  t1 = now();

  printf("%d puts, %.3f seconds, %.0f puts/second\n",
         NKEYS, t1 - t0, NKEYS / (t1 - t0));

  //
  // now the gets
  //
  t0 = now();
  for(int i = 0; i < nthread; i++) {
    assert(pthread_create(&tha[i], NULL, get_thread, (void *) (long) i) == 0);
  }
  for(int i = 0; i < nthread; i++) {
    assert(pthread_join(tha[i], &value) == 0);
  }
  t1 = now();

  printf("%d gets, %.3f seconds, %.0f gets/second\n",
         NKEYS*nthread, t1 - t0, (NKEYS*nthread) / (t1 - t0));

  destroy_locks();
}

/* Wrappers for mutex functions. */

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
