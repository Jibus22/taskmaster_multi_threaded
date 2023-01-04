#include <errno.h>
#include <pthread.h>

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

#define TM_LOGFILE "./taskmaster.log"

static uint8_t init_node(t_tm_node *node) {
  if (sem_init(&node->new_event, 0, 0) == -1) goto_error("sem_init");
  if (sem_init(&node->free_place, 0, LEN_EV_QUEUE) == -1)
    goto_error("sem_init");
  if (!(node->tm_stream_log = fopen(TM_LOGFILE, "a"))) goto_error("fopen");
  return EXIT_SUCCESS;
error:
  return EXIT_FAILURE;
}

int main(int ac, char **av) {
  t_tm_node node = {
      .tm_name = av[0],
      .mtx_log = PTHREAD_MUTEX_INITIALIZER,
      .mtx_queue = PTHREAD_MUTEX_INITIALIZER,
  };

  if (init_node(&node)) return EXIT_FAILURE;
  if (get_options(ac, av, &node)) return EXIT_FAILURE;
  if (init_taskmaster(&node)) return EXIT_FAILURE;
  if (run_server(&node)) return EXIT_FAILURE;
  run_client(&node);
  print_pgm_list(node.head);
  destroy_taskmaster(&node);
  return EXIT_SUCCESS;
}
