#include "ft_readline.h"
#include "taskmaster.h"

#define TM_CMD_NB (7)
#define TM_CMD_BUF_SZ (32)

#define MAX_CMD_ARG (5)

typedef uint8_t (*cmd_handler)(const t_tm_node *node, void *command);

typedef enum cmd_flag { NO_ARGS, ONE_ARG, MANY_ARGS } t_cmd_flag;

typedef struct s_tm_cmd {
    cmd_handler handler;      /* handler for the command 'name' */
    char name[TM_CMD_BUF_SZ]; /* name of the command */
    t_cmd_flag flag; /* how many arguments the command is supposed to accept */
    char *args[MAX_CMD_ARG]; /* pointers to arguments */
} t_tm_cmd;

static void *destroy_str_array(char **array, uint32_t sz) {
    while (--sz >= 0) {
        free(array[sz]);
        array[sz] = NULL;
    }
    return NULL;
}

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

#define DECL_CMD_HANDLER(name) \
    static uint8_t name(const t_tm_node *node, void *arg)

/* status can have 0 or 1 argument */
DECL_CMD_HANDLER(cmd_status) {
    t_tm_cmd *cmd = arg;
    printf("args: |%s|\n", cmd->name);

    /* j'ai des arguments qui correspondent au pgm à checker, en vrai je devrais
     * peu-être enregistrer l(es) index du maillon concerné ? */
    return EXIT_SUCCESS;
}

/* start has one argument which must match with a pgm name */
DECL_CMD_HANDLER(cmd_start) {
    t_tm_cmd *cmd = arg;
    printf("args: |%s|\n", cmd->name);

    return EXIT_SUCCESS;
}

/* start has one argument which must match with a pgm name */
DECL_CMD_HANDLER(cmd_stop) {
    t_tm_cmd *cmd = arg;
    printf("args: |%s|\n", cmd->name);
    return EXIT_SUCCESS;
}

/* reload config has 0 argument */
DECL_CMD_HANDLER(cmd_reload) {
    t_tm_cmd *cmd = arg;
    printf("args: |%s|\n", cmd->name);
    return EXIT_SUCCESS;
}

/* restart has one argument which must match with a pgm name */
DECL_CMD_HANDLER(cmd_restart) {
    t_tm_cmd *cmd = arg;
    printf("args: |%s|\n", cmd->name);
    return EXIT_SUCCESS;
}

/* exit has 0 argument */
DECL_CMD_HANDLER(cmd_exit) {
    t_tm_cmd *cmd = arg;
    printf("args: |%s|\n", cmd->name);
    return EXIT_SUCCESS;
}

/* help has 0 argument */
DECL_CMD_HANDLER(cmd_help) {
    t_tm_cmd *cmd = arg;
    printf("args: |%s|\n", cmd->name);
    return EXIT_SUCCESS;
}

#define CMD_ERR_OFFSET (-42)
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

static int32_t arg_error(t_tm_cmd *command, int32_t err) {
    bzero(command->args, sizeof(command->args));
    return err;
}

#define CMD_ERR_BUFSZ (32)
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
    int32_t i = 0, match_nb = 0, word_start, arg_len;
    bool found;

    while (args[i] == ' ') i++;
    while (args[i]) {
        found = false;
        if (command->flag == NO_ARGS) return CMD_TOO_MANY_ARGS;
        word_start = i;
        for (pgm = node->head; pgm && !found; pgm = pgm->privy.next) {
            arg_len = strlen(pgm->usr.name);
            if (!strncmp(args + word_start, pgm->usr.name, arg_len) &&
                (args[word_start + arg_len] == ' ' ||
                 args[word_start + arg_len] == 0)) {
                if (match_nb == MAX_CMD_ARG)
                    return arg_error(command, CMD_TOO_MANY_ARGS);
                command->args[match_nb] = (char *)(args + word_start);
                match_nb++;
                found = true;
                i += arg_len;
            }
        }
        if (!found) return arg_error(command, CMD_BAD_ARG);
        while (args[i] == ' ') i++;
    }
    if (command->flag == MANY_ARGS && !match_nb)
        return arg_error(command, CMD_ARG_MISSING);
    return EXIT_SUCCESS;
}

/* search for a registered command & sanitize its args */
static int32_t find_cmd(const t_tm_node *node, t_tm_cmd *command,
                        const char *line) {
    int32_t word_start = 0, cmd_len, ret;

    while (line[word_start] && line[word_start] == ' ') word_start++;
    if (!line[word_start]) return CMD_EMPTY_LINE;
    for (int32_t i = 0; i < TM_CMD_NB; i++) {
        cmd_len = strlen(command[i].name);
        if (!strncmp(line + word_start, command[i].name, cmd_len) &&
            (line[word_start + cmd_len] == ' ' ||
             !line[word_start + cmd_len])) {
            ret = sanitize_arg(node, &command[i], line + word_start + cmd_len);
            return ((i * (ret >= 0)) + (ret * (ret < 0)));
        }
    }
    return CMD_NOT_FOUND;
}

uint8_t run_client(t_tm_node *node) {
    char *line = NULL;
    char **completion = NULL;
    int32_t cmd_nb = TM_CMD_NB + node->pgm_nb, hdlr_type;
    t_tm_cmd commands[TM_CMD_NB] = {{cmd_status, "status", MANY_ARGS, {0}},
                                    {cmd_start, "start", MANY_ARGS, {0}},
                                    {cmd_stop, "stop", MANY_ARGS, {0}},
                                    {cmd_restart, "restart", MANY_ARGS, {0}},
                                    {cmd_reload, "reload", NO_ARGS, {0}},
                                    {cmd_exit, "exit", NO_ARGS, {0}},
                                    {cmd_help, "help", NO_ARGS, {0}}};

    completion = get_completion(node, commands, cmd_nb);
    ft_readline_add_completion(completion, cmd_nb);

    while ((line = ft_readline("taskmaster> ")) != NULL) {
        printf("You wrote: |%s|\n", line);
        ft_readline_add_history(line);
        hdlr_type = find_cmd(node, commands, line);

        if (hdlr_type >= 0)
            commands[hdlr_type].handler(node, &commands[hdlr_type]);
        else if (hdlr_type == CMD_EMPTY_LINE) { /* empty or space-filled line */
            free(line);
            continue;
        } else
            err_usr_input(node, hdlr_type);
        free(line);
    }
    return EXIT_SUCCESS;
}

/* 3- interpréter les commandes
 * 4- Pour le ctrl-z je le raise donc plus qu'à le gérer */
