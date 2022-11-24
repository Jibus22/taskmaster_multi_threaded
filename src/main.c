#include "taskmaster.h"

int main() {
  t_tm_node node = {0};

  if (load_config_file(&node)) return EXIT_FAILURE;
  /* sanitize_config(&node) */
  /* complete_config(&node) */
  /* run_server(&node) */
  /* run_client(&node) */
  return EXIT_SUCCESS;
}
