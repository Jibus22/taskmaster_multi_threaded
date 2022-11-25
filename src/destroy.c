#include "taskmaster.h"

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

static void destroy_pgm_private_attributes(t_pgm_private *pgm) {
  if (pgm->log.out > 0) close(pgm->log.out);
  if (pgm->log.err > 0) close(pgm->log.err);
  bzero(pgm, sizeof(*pgm));
}

void destroy_pgm_list(t_pgm **head) {
  t_pgm *next;

  while (*head) {
    next = (*head)->privy.next;
    destroy_pgm_user_attributes(&(*head)->usr);
    destroy_pgm_private_attributes(&(*head)->privy);
    DESTROY_PTR(*head);
    *head = next;
  }
  *head = NULL;
}

void destroy_taskmaster(t_tm_node *node) {
  fclose(node->config_file);
  destroy_pgm_list(&node->head);
  bzero(node, sizeof(*node));
}
