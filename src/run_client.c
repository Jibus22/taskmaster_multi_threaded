#include "run_client.h"

#include <pthread.h>

#include "ft_readline.h"
#include "run_server.h"

/* =============================== initialization =========================== */

static void *destroy_str_array(char **array, uint32_t sz) {
    while (--sz >= 0) {
        free(array[sz]);
        array[sz] = NULL;
    }
    return NULL;
}

/* Add taskmaster commands and program names to completion */
static char **get_completion(const t_tm_node *node, const t_tm_cmd *commands,
                             uint32_t cmd_nb) {
    uint32_t i = 0;
    t_pgm *pgm = node->head;
    char **completions = malloc(cmd_nb * sizeof(*completions));

    if (!completions) return NULL;

    while (i < TM_CMD_NB) {
        completions[i] = strdup(commands[i].name);
        if (!completions[i]) return destroy_str_array(completions, i);
        i++;
    }
    while (i < cmd_nb && pgm) {
        completions[i] = strdup(pgm->usr.name);
        if (!completions[i]) return destroy_str_array(completions, i);
        pgm = pgm->privy.next;
        i++;
    }
    return completions;
}

/* ============================== command handlers ========================== */

static char *get_next_word(const char *str) {
    int32_t i = 0;

    while (str[i] == ' ') i++;
    if (i) return (char *)(str + i);
    while (str[i] && str[i] != ' ') i++;
    while (str[i] == ' ') i++;
    if (!str[i]) return NULL;
    return (char *)(str + i);
}

static void add_event(t_tm_node *node, t_event event) {
    UNUSED_PARAM(node);
    sem_wait(&node->free_place);
    pthread_mutex_lock(&node->mtx_queue);
    node->event_queue[node->ev_queue_sz] = event;
    node->ev_queue_sz++;
    pthread_mutex_unlock(&node->mtx_queue);
    sem_post(&node->new_event);
}

/* Compare pgm names with the current argument and returns the corresponding
 * pgm adress if it match */
static t_pgm *get_pgm(const t_tm_node *node, char **args) {
    if (!*args) return NULL;
    for (t_pgm *pgm = node->head; pgm; pgm = pgm->privy.next) {
        if (!strncmp(pgm->usr.name, *args, strlen(pgm->usr.name))) {
            *args = get_next_word(*args);
            return pgm;
        }
    }
    return NULL;
}

/* status can have 0 or 1 argument */
DECL_CMD_HANDLER(cmd_status) {
    t_tm_cmd *cmd = command;
    char *args = cmd->args;
    t_pgm *pgm;
    t_thread_data *thrd;
    const char state[4][16] = {"stopped", "started", "starting", "stopping"};
    int32_t proc_st;

    if (cmd->args) {
        while ((pgm = get_pgm(node, &args))) {
            printf("- %s:\n", pgm->usr.name);
            for (int32_t i = pgm->usr.numprocs - 1; i >= 0; i--) {
                thrd = &(pgm->privy.thrd[i]);
                proc_st = ((GET_PROC_STATE == PROC_ST_STARTED) * 1) +
                          ((GET_PROC_STATE == PROC_ST_STARTING) * 2) +
                          ((GET_PROC_STATE == PROC_ST_STOPPING) * 3);
                printf("pid <%d> - state <%s>\n", thrd->pid, state[proc_st]);
            }
        }
    } else {
        for (pgm = node->head; pgm; pgm = pgm->privy.next) {
            uint32_t started = 0;
            for (int32_t i = pgm->usr.numprocs - 1; i >= 0; i--) {
                thrd = &(pgm->privy.thrd[i]);
                started = started + (GET_PROC_STATE == PROC_ST_STARTED);
            }
            printf("%s - run <%u/%u>\n", pgm->usr.name, started,
                   pgm->usr.numprocs);
        }
    }
    fflush(stdout);
    return EXIT_SUCCESS;
}

/* start has many arguments which must match with a pgm name */
DECL_CMD_HANDLER(cmd_start) {
    t_tm_cmd *cmd = command;
    char *args = cmd->args;
    t_pgm *pgm;

    while ((pgm = get_pgm(node, &args)))
        add_event(node, (t_event){pgm, CLIENT_START});
    return EXIT_SUCCESS;
}

/* stop has many arguments which must match with a pgm name */
DECL_CMD_HANDLER(cmd_stop) {
    t_tm_cmd *cmd = command;
    char *args = cmd->args;
    t_pgm *pgm;

    while ((pgm = get_pgm(node, &args)))
        add_event(node, (t_event){pgm, CLIENT_STOP});
    return EXIT_SUCCESS;
}

/* restart has many arguments which must match with a pgm name */
DECL_CMD_HANDLER(cmd_restart) {
    t_tm_cmd *cmd = command;
    char *args = cmd->args;
    t_pgm *pgm;

    while ((pgm = get_pgm(node, &args)))
        add_event(node, (t_event){pgm, CLIENT_RESTART});
    return EXIT_SUCCESS;
}

/* reload config has 0 argument */
DECL_CMD_HANDLER(cmd_reload) {
    UNUSED_PARAM(node);
    UNUSED_PARAM(command);
    // appeler la fonction reload que j'ai faite dans l'autre tm
    return EXIT_SUCCESS;
}

/* exit has 0 argument */
DECL_CMD_HANDLER(cmd_exit) {
    UNUSED_PARAM(command);
    add_event(node, (t_event){NULL, CLIENT_EXIT});
    node->exit_maint = true;
    return EXIT_SUCCESS;
}

/* help has 0 argument */
DECL_CMD_HANDLER(cmd_help) {
    UNUSED_PARAM(node);
    UNUSED_PARAM(command);
    fputs(
        "start <name>\t\tStart processes\n"
        "stop <name>\t\tStop processes\n"
        "restart <name>\t\tRestart all processes\n"
        "status <name>\t\tGet status for <name> processes\n"
        "status\t\tGet status for all programs\n"
        "exit\t\tExit the taskmaster shell and server.\n",
        stdout);
    fflush(stdout);
    return EXIT_SUCCESS;
}

/* ========================== user input sanitizer ========================== */

static void err_usr_input(t_tm_node *node, int32_t err) {
    static const char cmd_errors[CMD_ERR_NB][CMD_ERR_BUFSZ] = {
        "\0",
        "empty line",
        "command not found",
        "too many arguments",
        "argument missing",
        "bad argument"};

    err -= CMD_ERR_OFFSET; /* make err code start from 0 instead of being neg */
    fprintf(stderr, "%s: command error: %s\n", node->tm_name, cmd_errors[err]);
}

/* Checks number and validity of arguments according to the command */
static int32_t sanitize_arg(const t_tm_node *node, t_tm_cmd *command,
                            const char *args) {
    t_pgm *pgm;
    int32_t i = 0, arg_len;
    uint32_t match_nb = 0;
    bool found;

    while (args[i] == ' ') i++;
    while (args[i]) {
        found = false;
        if (command->flag == NO_ARGS) return CMD_TOO_MANY_ARGS;

        for (pgm = node->head; pgm && !found; pgm = pgm->privy.next) {
            arg_len = strlen(pgm->usr.name);
            if (!strncmp(args + i, pgm->usr.name, arg_len) &&
                (args[i + arg_len] == ' ' || args[i + arg_len] == 0)) {
                if (match_nb == node->pgm_nb) return CMD_TOO_MANY_ARGS;
                if (!match_nb) command->args = (char *)(args + i);
                match_nb++;
                found = true;
                i += arg_len;
            }
        }

        if (!found) return CMD_BAD_ARG;
        while (args[i] == ' ') i++;
    }

    if (command->flag == MANY_ARGS && !match_nb) return CMD_ARG_MISSING;
    return EXIT_SUCCESS;
}

/* Search for a registered command & sanitize its args */
static int32_t find_cmd(const t_tm_node *node, t_tm_cmd *command,
                        const char *line) {
    int32_t cmd_len, ret;

    if (!line[0]) return CMD_EMPTY_LINE;
    for (int32_t i = 0; i < TM_CMD_NB; i++) {
        cmd_len = strlen(command[i].name);
        if (!strncmp(line, command[i].name, cmd_len) &&
            (line[cmd_len] == ' ' || !line[cmd_len])) {
            ret = sanitize_arg(node, &command[i], line + cmd_len);
            return ((i * (ret >= 0)) + (ret * (ret < 0)));
        }
    }
    return CMD_NOT_FOUND;
}

/* Always append a NULL char at the end of dst, size should be at least
 * stlren(src)+1 */
static size_t strcpy_safe(char *dst, const char *src, size_t size) {
    uint32_t i = 0;

    if (!size) return strlen(src);
    while (src[i] && i + 1 < size) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
    if (!src[i]) return i;
    return strlen(src);
}

/* Remove extra spaces */
static void format_user_input(char *line) {
    int32_t i = 0, space;

    while (line[i] == ' ') i++;
    if (!line[i]) {
        line[0] = 0;
        return;
    }
    if (i) strcpy_safe(line, line + i, strlen(line + i) + 1);

    i = 0;
    while (line[i]) {
        space = 0;
        while (line[i] == ' ') {
            space++;
            i++;
        }
        if (!line[i]) {
            line[i - space] = 0;
            return;
        }
        if (space > 1) {
            strcpy_safe(line + (i - space + 1), line + i, strlen(line + i) + 1);
            i = i - space + 1;
        }
        i += (space == 0);
    }
}

/* ============================= client engine ============================== */

/* Reset args of command */
static inline void clean_command(t_tm_cmd *command) {
    for (int32_t i = 0; i < TM_CMD_NB; i++) command[i].args = NULL;
}

#include "run_server.h"

/* Main client function. Reads, sanitize & execute client input */
uint8_t run_client(t_tm_node *node) {
    char *line = NULL;
    char **completion = NULL;
    int32_t cmd_nb = TM_CMD_NB + node->pgm_nb, hdlr_type;
    t_tm_cmd command[TM_CMD_NB] = {{cmd_status, "status", FREE_NB_ARGS, 0},
                                   {cmd_start, "start", MANY_ARGS, 0},
                                   {cmd_stop, "stop", MANY_ARGS, 0},
                                   {cmd_restart, "restart", MANY_ARGS, 0},
                                   {cmd_reload, "reload", NO_ARGS, 0},
                                   {cmd_exit, "exit", NO_ARGS, 0},
                                   {cmd_help, "help", NO_ARGS, 0}};

    completion = get_completion(node, command, cmd_nb);
    ft_readline_add_completion(completion, cmd_nb);

    while (!node->exit_maint && (line = ft_readline("taskmaster$ ")) != NULL) {
        ft_readline_add_history(line);
        format_user_input(line); /* maybe use this only to send to a client */
        hdlr_type = find_cmd(node, command, line);

        if (hdlr_type >= 0) {
            command[hdlr_type].handler(node, &command[hdlr_type]);
        } else if (hdlr_type != CMD_EMPTY_LINE)
            err_usr_input(node, hdlr_type);
        clean_command(command);
        free(line);
    }
    if (pthread_join(node->master_thrd, NULL)) perror("pthread_join");
    return EXIT_SUCCESS;
}
