#include <stdlib.h>
#include "wq.h"
#include "utlist.h"

pthread_mutex_t wq_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond = PTHREAD_COND_INITIALIZER;

/* Initializes a work queue WQ. */

  /* TODO: Make me thread-safe! */

void wq_init(wq_t *wq) {
  pthread_mutex_lock(&wq_lock);
  wq->size = 0;
  wq->head = NULL;
  pthread_mutex_unlock(&wq_lock);
}

/* Remove an item from the WQ. This function should block until there
 * is at least one item on the queue. */

  /* TODO: Make me blocking and thread-safe! */
int wq_pop(wq_t *wq) {

  pthread_mutex_lock(&wq_lock);
  while (wq->size <= 0) {
    pthread_cond_wait(&cond, &wq_lock);
  }

  wq_item_t *wq_item = wq->head;
  int client_socket_fd = wq->head->client_socket_fd;
  wq->size--;
  DL_DELETE(wq->head, wq->head);

  pthread_mutex_unlock(&wq_lock);
  free(wq_item);
  return client_socket_fd;
}

/* Add ITEM to WQ. */
void wq_push(wq_t *wq, int client_socket_fd) {
  pthread_mutex_lock(&wq_lock);

  wq_item_t *wq_item = calloc(1, sizeof(wq_item_t));
  wq_item->client_socket_fd = client_socket_fd; // Setting Client Socket fd
  DL_APPEND(wq->head, wq_item);
  wq->size++;
  pthread_cond_signal(&cond);

  pthread_mutex_unlock(&wq_lock);
}