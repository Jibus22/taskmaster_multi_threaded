#ifndef TM_PARSING_H
#define TM_PARSING_H

#include <inttypes.h>

#define SIGNAL_NB (31)   /* number of posix signal */
#define KEY_BUF_LEN (32) /* buffer size to store a key name */

/* different keys for a config file */
enum e_keys {
  KEY_CMD,
  KEY_ENV,
  KEY_STDOUT,
  KEY_STDERR,
  KEY_WORKINGDIR,
  KEY_EXITCODES,
  KEY_NUMPROCS,
  KEY_UMASK,
  KEY_AUTORESTART,
  KEY_STARTRETRIES,
  KEY_AUTOSTART,
  KEY_STOPSIGNAL,
  KEY_STARTTIME,
  KEY_STOPTIME,
  KEY_NB_MAX, /* number of keys in a config file */
};

#define AUTORESTART_BUF_SIZE                      \
  (32) /* buffer size to store a autorestart name \
        */

#define SEC_TO_MS (1000)

typedef struct s_config_parsing {
  uint8_t info; /* bit interrupt to detect '+STR - +DOC - +MAP' start sequence*/
  uint8_t scalar_type; /* is it a key or a value */
  int8_t key;          /* key number */
  uint8_t map_depth;   /* increments when a new field appears at a new level */
  uint8_t seq_depth;
} t_config_parsing;

#define IS_KEY (parsing->scalar_type == 0)
#define IS_VALUE (parsing->scalar_type == 1)
#define TOGGLE_SCALAR_TYPE            \
  do {                                \
    parsing->scalar_type ^= (1 << 0); \
  } while (0)

#define KEY_TYPE (0)
#define VALUE_TYPE (1)
#define TOGGLE_TYPE(value) \
  do {                     \
    value ^= (1 << 0);     \
  } while (0)

typedef enum e_parsing_info_mask {
  MASK_STREAM = (1 << 0),
  MASK_DOC = (1 << 1),
  MASK_PGM = (1 << 2),
} t_parsing_info_mask;

#define PARSING_READY \
  (0x7) /* value of t_config_parsing::info when all bits are set (0000 0111)*/

#define YAML_MAX_EVENT (YAML_MAPPING_END_EVENT + 1)
#define YAML_MAX_SCALAR_EVENT (5)
#define DECL_YAML_HANDLER(name)                                   \
  static uint8_t name(t_tm_node *node, t_config_parsing *parsing, \
                      yaml_event_t *event)
#define DECL_DATA_LOAD_HANDLER(name) \
  static uint8_t name(t_pgm *pgm, const char *data)

#define SAN_NUM_PROC_MAX (30)
#define SAN_RETRIES_MAX (128)
#define SAN_STARTTIME_MAX (120) /* in seconds */
#define SAN_STOPTIME_MAX (60)   /* in seconds */

#define ERR_TYPE_BUF_SIZE (48)
#define ERR_MSG_BUF_SIZE (256)

typedef enum e_parsing_error {
  NO_ERROR,
  UNDEFINED_ERROR,
  WRONG_KEY,
  NUMPROC_ERR,
  UMASK_ERR,
  AUTORESTART_ERR,
  RETRIES_ERR,
  AUTOSTART_ERR,
  WRONG_STOP_SIGNAL,
  STARTTIME_ERR,
  STOPTIME_ERR,
  CMD_MISS,
  ENV_MISS,
  STDOUT_MISS,
  STDERR_MISS,
  WD_MISS,
  EXITCODE_MISS,
  NUMPROC_MISS,
  UMASK_MISS,
  AUTORESTART_MISS,
  STARTRETRIES_MISS,
  STARTTIME_MISS,
  STOPSIGNAL_MISS,
  STOPTIME_MISS,
  PARSING_ERROR_NB_MAX,
} t_parsing_error;

#define DESTROY_PTR(ptr) \
  do {                   \
    if ((ptr)) {         \
      free((ptr));       \
      ptr = NULL;        \
    }                    \
  } while (0)

#endif

/*
** Below is how yaml events looks like when parsing a .yaml file.
**
** +STR
** +DOC
** +MAP
** =VAL :programs
** +MAP
** =VAL :daemon_ALPHA
** +MAP
** =VAL :cmd
** =VAL "/home/user42/42/taskmaster/test/daemons/daemon_ALPHA arg1 arg2
** =VAL :numprocs
** =VAL :5
** =VAL :umask
** =VAL :777
** =VAL :workingdir
** =VAL :/tmp
** =VAL :autostart
** =VAL :true
** =VAL :autorestart
** =VAL :unexpected
** =VAL :exitcodes
** +SEQ
** =VAL :0
** =VAL :2
** -SEQ
** =VAL :startretries
** =VAL :1
** =VAL :starttime
** =VAL :2
** =VAL :stopsignal
** =VAL :TERM
** =VAL :stoptime
** =VAL :8
** =VAL :stdout
** =VAL :/tmp/alpha.stdout
** =VAL :stderr
** =VAL :/tmp/alpha.stderr
** =VAL :env
** +MAP
** =VAL :STARTED_BY
** =VAL :taskmaster
** =VAL :ANSWER
** =VAL :42
** -MAP
** -MAP
** -MAP
** -MAP
** -DOC
** -STR
*/
