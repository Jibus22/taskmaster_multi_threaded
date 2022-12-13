#include "taskmaster.h"

static uint8_t usage(char *const *av) {
  fprintf(stderr, "Usage: %s [-f filename]\n", av[0]);
  return EXIT_FAILURE;
}

static uint8_t get_options(int ac, char *const *av, t_tm_node *node) {
  int32_t opt;

  while ((opt = getopt(ac, av, "f:")) != -1) {
    switch (opt) {
      case 'f':
        if (!(node->config_file = fopen(optarg, "r"))) {
          fprintf(stderr, "%s: %s: %s\n", av[0], optarg, strerror(errno));
          return EXIT_FAILURE;
        }
        break;
      case '?':
      default:
        return usage(av);
    }
  }

  if (ac < 2 || optind < ac) return usage(av);

  return EXIT_SUCCESS;
}

int main(int ac, char **av) {
  t_tm_node node = {.tm_name = av[0], 0};

  if (get_options(ac, av, &node)) return EXIT_FAILURE;
  if (load_config_file(&node)) return EXIT_FAILURE;
  if (sanitize_config(&node)) return EXIT_FAILURE;
  if (fulfill_config(&node)) return EXIT_FAILURE;
  /* run_server(&node) */
  run_client(&node);
  print_pgm_list(node.head);
  destroy_taskmaster(&node);
  return EXIT_SUCCESS;
}
