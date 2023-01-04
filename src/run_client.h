#ifndef RUN_CLIENT_H
#define RUN_CLIENT_H

#include "taskmaster.h"

#define TM_CMD_NB (7)      /* number of commands of taskmaster */
#define TM_CMD_BUF_SZ (32) /* buf size to store command names */

typedef uint8_t (*cmd_handler)(t_tm_node *node, void *command);

typedef enum cmd_flag { NO_ARGS, FREE_NB_ARGS, MANY_ARGS } t_cmd_flag;

typedef struct s_tm_cmd {
    const cmd_handler handler;      /* handler for the command 'name' */
    const char name[TM_CMD_BUF_SZ]; /* name of the command */
    const t_cmd_flag
        flag;   /* how many arguments the command is supposed to accept */
    char *args; /* pointer to arguments */
} t_tm_cmd;

/* generic declaration for command handlers */
#define DECL_CMD_HANDLER(name) \
    static uint8_t name(t_tm_node *node, void *command)

#define CMD_ERR_OFFSET (-42) /* useful to set errors to positive values */
#define CMD_ERR_NB (CMD_MAX - CMD_ERR_OFFSET)
typedef enum e_cmd_err {
    CMD_NO_ERR = CMD_ERR_OFFSET,
    CMD_EMPTY_LINE,
    CMD_NOT_FOUND,
    CMD_TOO_MANY_ARGS,
    CMD_ARG_MISSING,
    CMD_BAD_ARG,
    CMD_MAX,
} t_cmd_err;

#define CMD_ERR_BUFSZ (32) /* buffer size to store error names */

#endif
