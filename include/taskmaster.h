#ifndef TM_H
#define TM_H

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <netdb.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <term.h>
#include <unistd.h>

#include "yaml.h"

#define handle_error(msg) \
  do {                    \
    perror(msg);          \
    exit(EXIT_FAILURE);   \
  } while (0)

#define UNUSED_PARAM(a) (void)(a);

#define SIGNAL_BUF_SIZE 32
typedef struct s_signal {
  char name[SIGNAL_BUF_SIZE];
  uint8_t nb;
} t_signal;

typedef enum e_autorestart {
  autorestart_false,
  autorestart_true,
  autorestart_unexpected,
  autorestart_max
} t_autorestart;

typedef struct s_pgm {
  char *name; /* pgm name */
  char **cmd; /* launch command */
  struct {
    char **array_val; /* environment passed to the processus */
    uint16_t array_size;
  } env;
  char *std_out;    /* which file processus logs out (default /dev/null) */
  char *std_err;    /* which file processus logs err (default /dev/null) */
  char *workingdir; /* working directory of processus */
  struct s_exit_code {
    int16_t *array_val; /* array of expected exit codes */
    uint16_t array_size;
  } exitcodes;
  uint16_t numprocs;         /* number of processus to run */
  mode_t umask;              /* umask of processus (default permissions) */
  t_autorestart autorestart; /* autorestart permissions */
  uint8_t startretries;      /* how many times a processus can restart */
  bool autostart;            /* start at launch of taskmaster or not */
  t_signal stopsignal;       /* which signal to use when using 'stop' command */
  uint32_t starttime;        /* time until it is considered a processus is well
                                launched. in ms*/
  uint32_t stoptime; /* time allowed to a processus to stop before it is killed.
                      in ms*/

  struct s_pgm *next;
} t_pgm;

typedef struct s_tm_node {
  t_pgm *head;
} t_tm_node;

/* parsing.c */
uint8_t load_config_file(t_tm_node *node);

/* debug.c */
void print_pgm_list(t_pgm *pgm);

#endif
