#include <pthread.h>

#include "run_server.h"

static void destroy_pgm_user_attributes(t_pgm_usr *pgm) {
  DESTROY_PTR(pgm->name);
  if (pgm->cmd) {
    for (uint32_t i = 0; pgm->cmd[i]; i++) DESTROY_PTR(pgm->cmd[i]);
    DESTROY_PTR(pgm->cmd);
  }
  if (pgm->env.array_val) {
    for (uint32_t i = 0; i < pgm->env.array_size; i++)
      DESTROY_PTR(pgm->env.array_val[i]);
    DESTROY_PTR(pgm->env.array_val);
  }
  DESTROY_PTR(pgm->std_out);
  DESTROY_PTR(pgm->std_err);
  DESTROY_PTR(pgm->workingdir);
  DESTROY_PTR(pgm->exitcodes.array_val);
  bzero(pgm, sizeof(*pgm));
}

static void destroy_thrd(t_thread_data *thrd, int32_t numprocs) {
  t_thread_data *cpy = thrd;
  for (int i = 0; i < numprocs; i++) {
    pthread_rwlock_destroy(&thrd->rw_thrd);
    pthread_barrier_destroy(&thrd->sync_barrier);
    pthread_mutex_destroy(&thrd->mtx_wakeup);
    pthread_cond_destroy(&thrd->cond_wakeup);
    sem_destroy(&thrd->sync);
    pthread_mutex_destroy(&thrd->mtx_timer);
    pthread_cond_destroy(&thrd->cond_timer);
    thrd++;
  }
  free(cpy);
}

static void destroy_pgm_private_attributes(t_pgm_private *pgm,
                                           int32_t numprocs) {
  if (pgm->log.out > 0) close(pgm->log.out);
  if (pgm->log.err > 0) close(pgm->log.err);
  if (pgm->thrd) {
    pthread_rwlock_destroy(&pgm->rw_pgm);
    destroy_thrd(pgm->thrd, numprocs);
  }
  bzero(pgm, sizeof(*pgm));
}

void destroy_pgm(t_pgm *pgm) {
  destroy_pgm_user_attributes(&pgm->usr);
  destroy_pgm_private_attributes(&pgm->privy, pgm->usr.numprocs);
  DESTROY_PTR(pgm);
}

void destroy_pgm_list(t_pgm **head) {
  t_pgm *next;

  while (*head) {
    next = (*head)->privy.next;
    destroy_pgm(*head);
    *head = next;
  }
  *head = NULL;
}

void destroy_taskmaster(t_tm_node *node) {
  fclose(node->config_file);
  destroy_pgm_list(&node->head);
  sem_destroy(&node->new_event);
  sem_destroy(&node->free_place);
  fclose(node->tm_stream_log);
  bzero(node, sizeof(*node));
}
