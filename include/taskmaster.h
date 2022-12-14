#ifndef TASKMASTER_H
#define TASKMASTER_H

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define handle_error(msg) \
  do {                    \
    perror(msg);          \
    exit(EXIT_FAILURE);   \
  } while (0)

#define goto_error(msg) \
  do {                  \
    perror(msg);        \
    goto error;         \
  } while (0)

#define DESTROY_PTR(ptr) \
  do {                   \
    if ((ptr)) {         \
      free((ptr));       \
      ptr = NULL;        \
    }                    \
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

typedef struct s_pgm_usr {
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
  uint32_t stoptime;         /* time allowed to a processus to stop before it is
                              killed. in ms*/
} t_pgm_usr;

typedef struct s_pgm_private {
  struct log {
    int32_t out;
    int32_t err;
  } log;
  struct s_pgm *next;
} t_pgm_private;

typedef struct s_pgm {
  t_pgm_usr usr;
  t_pgm_private privy;
} t_pgm;

typedef struct s_tm_node {
  char *tm_name;
  FILE *config_file;
  t_pgm *head;
  uint32_t pgm_nb;
} t_tm_node;

/* parsing.c */
uint8_t load_config_file(t_tm_node *node);
uint8_t sanitize_config(t_tm_node *node);
uint8_t fulfill_config(t_tm_node *node);

/* run_client.c */
uint8_t run_client(t_tm_node *node);

/* debug.c */
void print_pgm_list(t_pgm *pgm);

/* destroy.c */
void destroy_pgm_list(t_pgm **head);
void destroy_taskmaster(t_tm_node *node);

#endif
