#ifndef TASKMASTER_H
#define TASKMASTER_H

#include <inttypes.h>
#include <semaphore.h>
#include <stdatomic.h>
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
#define UNINITIALIZED_FD (-42)

#define SIGNAL_BUF_SIZE (32) /* buffer size to store signal name */
typedef struct s_signal {
  char name[SIGNAL_BUF_SIZE]; /* name of signal */
  uint8_t nb;                 /* number corresponding to the signal */
} t_signal;

typedef enum e_autorestart {
  autorestart_false,
  autorestart_true,
  autorestart_unexpected,
  autorestart_max
} t_autorestart;

/* data of a program fetch in config file */
typedef struct s_pgm_usr {
  char *name; /* pgm name */
  char **cmd; /* launch command */
  struct {
    char **array_val; /* environment passed to the processus */
    uint32_t array_size;
  } env;
  char *std_out;    /* which file processus logs out (default /dev/null) */
  char *std_err;    /* which file processus logs err (default /dev/null) */
  char *workingdir; /* working directory of processus */
  struct s_exit_code {
    int32_t *array_val; /* array of expected exit codes */
    uint32_t array_size;
  } exitcodes;
  uint32_t numprocs;         /* number of processus to run */
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

typedef struct thread_data t_thread_data;

/* data of a program dynamically filled at runtime for taskmaster operations */
typedef struct s_pgm_private {
  struct log {
    int32_t out; /* fd for logging out */
    int32_t err; /* fd for logging err */
  } log;
  pthread_rwlock_t rw_pgm;
  /* atomic_uint nb_thread_alive; */
  struct timeval stop_timestamp;
  t_thread_data *thrd; /* array of t_thread_data */
  struct s_pgm *next;  /* next link of the linked list */
} t_pgm_private;

/* concatenation of all data a pgm need in taskmaster */
typedef struct s_pgm {
  t_pgm_usr usr;
  t_pgm_private privy;
} t_pgm;

/* event corresponding with client command */
typedef enum e_client_ev {
  CLIENT_STATUS,
  CLIENT_START,
  CLIENT_RESTART,
  CLIENT_STOP,
  CLIENT_EXIT,
  CLIENT_ADD,
  CLIENT_DEL,
  CLIENT_MAX_EVENT,
} t_client_ev;

typedef struct s_event {
  t_pgm *pgm;
  t_client_ev type;
} t_event;

#define LEN_EV_QUEUE (64U)

typedef struct s_tm_node {
  char *tm_name;     /* taskmaster name (argv[0]) */
  FILE *config_file; /* configuration file */
  t_pgm *head;       /* head of list of programs */
  uint32_t pgm_nb;   /* number of programs */
  pthread_t master_thrd;

  t_event event_queue[LEN_EV_QUEUE];
  pthread_mutex_t mtx_queue;
  sem_t new_event;
  sem_t free_place;
  uint32_t ev_queue_sz;

  pthread_mutex_t mtx_log;
  FILE *tm_stream_log; /* taskmaster file log */
  atomic_bool exit_mastt; /* exit master thread */
  atomic_bool exit_maint; /* exit main thread */
} t_tm_node;

/* parsing.c */
uint8_t init_taskmaster(t_tm_node *node);

/* run_server.c */
uint8_t run_server(t_tm_node *node);

/* run_client.c */
uint8_t run_client(t_tm_node *node);

/* debug.c */
void print_pgm_list(t_pgm *pgm);

/* destroy.c */
void destroy_pgm(t_pgm *pgm);
void destroy_pgm_list(t_pgm **head);
void destroy_taskmaster(t_tm_node *node);

#endif
