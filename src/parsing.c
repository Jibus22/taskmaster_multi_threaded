#include "parsing.h"

#include "taskmaster.h"

static const char err_type[CONFIG_ERROR_NB_MAX][ERR_TYPE_BUF_SIZE] = {
    "\0",
    "undefined error\0",
    "wrong key\0",
    "wrong value\0",
    "value missing\0",
};

static const char keys[KEY_NB_MAX][KEY_BUF_LEN] = {
    "\0",           "cmd\0",         "env\0",          "stdout\0",
    "stderr\0",     "workingdir\0",  "exitcodes\0",    "numprocs\0",
    "umask\0",      "autorestart\0", "startretries\0", "autostart\0",
    "stopsignal\0", "starttime\0",   "stoptime\0",
};

static t_config_error print_san_err(t_keys key, t_config_error err,
                                    const char *err_msg) {
  char err_msg_buf[ERR_MSG_BUF_SIZE] = {0};
  snprintf(err_msg_buf, ERR_MSG_BUF_SIZE, "Sanitize error: %s%s%s%s\n",
           keys[key], key ? " key: " : "", err_type[err],
           err_msg ? err_msg : "");
  write(STDERR_FILENO, err_msg_buf, strlen(err_msg_buf));
  return err;
}

static void handle_config_error(yaml_event_t *event, t_config_error err,
                                t_keys key) {
  char err_msg_buf[ERR_MSG_BUF_SIZE] = {0};

  snprintf(err_msg_buf, ERR_MSG_BUF_SIZE,
           "Parse error: %s%s%s\nLine: %lu Column: %lu\n", keys[key],
           key ? " key: " : "", err_type[err], event->start_mark.line + 1,
           event->start_mark.column + 1);
  write(STDERR_FILENO, err_msg_buf, strlen(err_msg_buf));
}

static const t_signal siglist[SIGNAL_NB] = {
    {"SIGHUP\0", 1},     /*  terminal line hangup */
    {"SIGINT\0", 2},     /*  interrupt program */
    {"SIGQUIT\0", 3},    /*  quit program */
    {"SIGILL\0", 4},     /*  illegal instruction */
    {"SIGTRAP\0", 5},    /*  trace trap */
    {"SIGABRT\0", 6},    /*  abort program (formerly SIGIOT) */
    {"SIGEMT\0", 7},     /*  emulate instruction executed */
    {"SIGFPE\0", 8},     /*  floating-point exception */
    {"SIGKILL\0", 9},    /*  kill program */
    {"SIGBUS\0", 10},    /*  bus error */
    {"SIGSEGV\0", 11},   /*  segmentation violation */
    {"SIGSYS\0", 12},    /*  non-existent system call invoked */
    {"SIGPIPE\0", 13},   /*  write on a pipe with no reader */
    {"SIGALRM\0", 14},   /*  real-time timer expired */
    {"SIGTERM\0", 15},   /*  software termination signal */
    {"SIGURG\0", 16},    /*  urgent condition present on socket */
    {"SIGSTOP\0", 17},   /*  stop (cannot be caught or ignored) */
    {"SIGTSTP\0", 18},   /*  stop signal generated from keyboard */
    {"SIGCONT\0", 19},   /*  continue after stop */
    {"SIGCHLD\0", 20},   /*  child status has changed */
    {"SIGTTIN\0", 21},   /*  background read attempted from control terminal */
    {"SIGTTOU\0", 22},   /*  background write attempted to control terminal */
    {"SIGIO\0", 23},     /*  I/O is possible on a descriptor (see fcntl(2)) */
    {"SIGXCPU\0", 24},   /*  cpu time limit exceeded (see setrlimit(2)) */
    {"SIGXFSZ\0", 25},   /*  file size limit exceeded (see setrlimit(2)) */
    {"SIGVTALRM\0", 26}, /*  virtual time alarm (see setitimer(2)) */
    {"SIGPROF\0", 27},   /*  profiling timer alarm (see setitimer(2)) */
    {"SIGWINCH\0", 28},  /*  Window size change */
    {"SIGINFO\0", 29},   /*  status request from keyboard */
    {"SIGUSR1\0", 30},   /*  User defined signal 1 */
    {"SIGUSR2\0", 31}    /*  User defined signal 2 */
};

static void *destroy_str_array(char **array, uint32_t cnt) {
  for (uint32_t i = 0; i < cnt; i++) free(array[i]);
  free(array);
  return NULL;
}

typedef void *(*t_cb)(char **, const char *, uint32_t, uint32_t);

/* return how many strings can be created and if a callback is set, process
 * it.
 */
static int32_t roam_process_string(const char *str, char c, char **array,
                                   t_cb callback) {
  uint32_t i = 0, cnt = 0, start;

  while (str[i] && str[i] == c) i++;
  start = i;
  while (str[i]) {
    if (str[i] == c) {
      if (callback)
        if (!callback(array, str + start, cnt, (i - start))) return -1;
      cnt++;
      while (str[i] == c) i++;
      start = i;
    } else {
      i++;
    }
  }
  if (start != i) {
    if (callback)
      if (!callback(array, str + start, cnt, (i - start))) return -1;
    cnt++;
  }
  return cnt;
}

static void *add_string(char **array, const char *str, uint32_t idx,
                        uint32_t len) {
  array[idx] = calloc((len + 1), sizeof(char));
  if (!array[idx]) return destroy_str_array(array, idx);
  strncpy(array[idx], str, len);
  return array[idx];
}

static char **ft_split(const char *str, char c) {
  uint32_t cnt = 0;
  char **array = NULL;

  cnt = roam_process_string(str, c, array, NULL);
  array = calloc((cnt + 1), sizeof(*array));
  if (!array) return NULL;
  if (roam_process_string(str, c, array, add_string) == -1) return NULL;
  return array;
}

DECL_DATA_LOAD_HANDLER(nokey_data_load) {
  UNUSED_PARAM(pgm);
  UNUSED_PARAM(data);
  return EXIT_FAILURE;
}

DECL_DATA_LOAD_HANDLER(cmd_data_load) {
  char *cmd;

  if (!*data) return MISSING_ERROR;
  cmd = strdup(data);
  if (!cmd) handle_error("strdup");
  pgm->cmd = ft_split(cmd, ' ');
  free(cmd);
  if (!pgm->cmd) handle_error("ft_split");
  return EXIT_SUCCESS;
}

DECL_DATA_LOAD_HANDLER(env_data_load) {
  static uint8_t data_type = KEY_TYPE;
  size_t data_len = strlen(data), old_len;
  char *str = NULL, *old_str;

  if (!*data) return MISSING_ERROR;

  if (data_type == KEY_TYPE) {
    /* create a new char pointer for a new key=value pair*/
    pgm->env.array_size++;
    pgm->env.array_val =
        realloc(pgm->env.array_val,
                (pgm->env.array_size + 1) * sizeof(*(pgm->env.array_val)));
    if (!pgm->env.array_val) handle_error("realloc");
    pgm->env.array_val[pgm->env.array_size] = NULL;

    /* create a new formated 'key' string and hold the address in the array */
    str = malloc((data_len + 2) * sizeof(*str));
    if (!str) handle_error("malloc");
    strncpy(str, data, data_len);
    strncpy(str + data_len, "=\0", 2);
    pgm->env.array_val[pgm->env.array_size - 1] = str;
  } else if (data_type == VALUE_TYPE) {
    /* just concatenate the value to the key string */
    old_str = pgm->env.array_val[pgm->env.array_size - 1];
    old_len = strlen(old_str);
    str = realloc(old_str, old_len + data_len + 1);
    if (!str) handle_error("realloc");
    strncpy(str + old_len, data, data_len);
    str[old_len + data_len] = 0;
    pgm->env.array_val[pgm->env.array_size - 1] = str;
  } else
    return EXIT_FAILURE;

  TOGGLE_TYPE(data_type);
  return EXIT_SUCCESS;
}

DECL_DATA_LOAD_HANDLER(stdout_data_load) {
  if (!*data) return MISSING_ERROR;
  pgm->std_out = strdup(data);
  if (!pgm->std_out) handle_error("strdup");
  return EXIT_SUCCESS;
}

DECL_DATA_LOAD_HANDLER(stderr_data_load) {
  if (!*data) return MISSING_ERROR;
  pgm->std_err = strdup(data);
  if (!pgm->std_err) handle_error("strdup");
  return EXIT_SUCCESS;
}

DECL_DATA_LOAD_HANDLER(workingdir_data_load) {
  if (!*data) return MISSING_ERROR;
  pgm->workingdir = strdup(data);
  if (!pgm->workingdir) handle_error("strdup");
  return EXIT_SUCCESS;
}

DECL_DATA_LOAD_HANDLER(exitcodes_data_load) {
  char *endptr;

  if (!*data) return MISSING_ERROR;
  if (!pgm->exitcodes.array_val) {
    pgm->exitcodes.array_size++;
    pgm->exitcodes.array_val =
        calloc(pgm->exitcodes.array_size, sizeof(*(pgm->exitcodes.array_val)));
    if (!pgm->exitcodes.array_val) handle_error("calloc");
    pgm->exitcodes.array_val[0] = (uint8_t)strtoimax(data, &endptr, 10);
  } else {
    pgm->exitcodes.array_size++;
    pgm->exitcodes.array_val =
        reallocarray(pgm->exitcodes.array_val, pgm->exitcodes.array_size,
                     sizeof(*(pgm->exitcodes.array_val)));
    if (!pgm->exitcodes.array_val) handle_error("reallocarray");
    pgm->exitcodes.array_val[pgm->exitcodes.array_size - 1] =
        (uint8_t)strtoimax(data, &endptr, 10);
  }
  return EXIT_SUCCESS;
}

DECL_DATA_LOAD_HANDLER(numprocs_data_load) {
  char *endptr;

  if (!*data) return MISSING_ERROR;
  pgm->numprocs = (uint16_t)strtoumax(data, &endptr, 10);
  if (pgm->numprocs > SAN_NUM_PROC_MAX) return VALUE_ERROR;
  return EXIT_SUCCESS;
}

DECL_DATA_LOAD_HANDLER(umask_data_load) {
  char *endptr;

  if (!*data) return MISSING_ERROR;
  pgm->umask = (mode_t)strtoumax(data, &endptr, 8);
  if (pgm->umask > 0777) return VALUE_ERROR;
  return EXIT_SUCCESS;
}

DECL_DATA_LOAD_HANDLER(autorestart_data_load) {
  uint32_t i = 0;
  static const char autorestart_keys[autorestart_max][AUTORESTART_BUF_SIZE] = {
      "false\0",
      "true\0",
      "unexpected\0",
  };

  if (!*data) return MISSING_ERROR;
  while (i < autorestart_max) {
    if (!strcmp(autorestart_keys[i], data)) {
      pgm->autorestart = i;
      break;
    }
    i++;
  }
  if (i == autorestart_max) return VALUE_ERROR;
  return EXIT_SUCCESS;
}

DECL_DATA_LOAD_HANDLER(startretries_data_load) {
  char *endptr;

  if (!*data) return MISSING_ERROR;
  pgm->startretries = (uint8_t)strtoumax(data, &endptr, 10);
  if (pgm->startretries > SAN_RETRIES_MAX) return VALUE_ERROR;
  return EXIT_SUCCESS;
}

DECL_DATA_LOAD_HANDLER(autostart_data_load) {
  if (!*data) return EXIT_SUCCESS;
  if (!strcmp("true\0", data))
    pgm->autostart = true;
  else if (!strcmp("false\0", data))
    pgm->autostart = false;
  else
    return VALUE_ERROR;
  return EXIT_SUCCESS;
}

DECL_DATA_LOAD_HANDLER(stopsignal_data_load) {
  uint32_t i = 0;

  if (!*data) return MISSING_ERROR;
  while (i < SIGNAL_NB) {
    if (!strcmp(siglist[i].name, data)) {
      pgm->stopsignal = siglist[i];
      break;
    }
    i++;
  }
  if (i == SIGNAL_NB) return VALUE_ERROR;
  return EXIT_SUCCESS;
}

DECL_DATA_LOAD_HANDLER(starttime_data_load) {
  char *endptr;

  if (!*data) return MISSING_ERROR;
  pgm->starttime = (uint32_t)strtoumax(data, &endptr, 10);
  if (pgm->starttime > SAN_STARTTIME_MAX) return VALUE_ERROR;
  pgm->starttime *= SEC_TO_MS;
  return EXIT_SUCCESS;
}

DECL_DATA_LOAD_HANDLER(stoptime_data_load) {
  char *endptr;

  if (!*data) return MISSING_ERROR;
  pgm->stoptime = (uint32_t)strtoumax(data, &endptr, 10);
  if (pgm->stoptime > SAN_STOPTIME_MAX) return VALUE_ERROR;
  pgm->stoptime *= SEC_TO_MS;
  return EXIT_SUCCESS;
}

/* array of functions of type DATA_LOAD_HANDLER */
uint8_t (*handle_data_loading[KEY_NB_MAX])(t_pgm_usr *, const char *) = {
    nokey_data_load,       cmd_data_load,          env_data_load,
    stdout_data_load,      stderr_data_load,       workingdir_data_load,
    exitcodes_data_load,   numprocs_data_load,     umask_data_load,
    autorestart_data_load, startretries_data_load, autostart_data_load,
    stopsignal_data_load,  starttime_data_load,    stoptime_data_load,
};

DECL_YAML_HANDLER(yaml_nothing) {
  UNUSED_PARAM(node);
  UNUSED_PARAM(parsing);
  UNUSED_PARAM(event);
  return EXIT_SUCCESS;
}

DECL_YAML_HANDLER(yaml_stream_st) {
  UNUSED_PARAM(node);
  UNUSED_PARAM(event);
  parsing->info |= MASK_STREAM;
  return EXIT_SUCCESS;
}

DECL_YAML_HANDLER(yaml_stream_e) {
  UNUSED_PARAM(node);
  UNUSED_PARAM(event);
  parsing->info &= ~MASK_STREAM;
  return EXIT_SUCCESS;
}

DECL_YAML_HANDLER(yaml_doc_st) {
  UNUSED_PARAM(node);
  UNUSED_PARAM(event);
  parsing->info |= MASK_DOC;
  return EXIT_SUCCESS;
}

DECL_YAML_HANDLER(yaml_doc_e) {
  UNUSED_PARAM(node);
  UNUSED_PARAM(event);
  parsing->info &= ~MASK_DOC;
  return EXIT_SUCCESS;
}

DECL_YAML_HANDLER(yaml_alias) {
  UNUSED_PARAM(node);
  UNUSED_PARAM(parsing);
  UNUSED_PARAM(event);
  return EXIT_SUCCESS;
}

DECL_YAML_HANDLER(yaml_scalar_0) {
  UNUSED_PARAM(node);
  UNUSED_PARAM(parsing);
  UNUSED_PARAM(event);
  return EXIT_FAILURE;
}

/* this depth of scalar event should happen only once and declare the begining
 * of programs declaration
 */
DECL_YAML_HANDLER(yaml_scalar_1) {
  UNUSED_PARAM(node);
  if (parsing->info != PARSING_READY) {
    if (!strcmp("programs\0", (char *)event->data.scalar.value)) {
      parsing->info |= MASK_PGM;
    } else {
      return EXIT_FAILURE; /* wrong key */
    }
  } else {
    return EXIT_FAILURE; /* We should enter only once in 'programs' field */
  }
  return EXIT_SUCCESS;
}

/* this depth of scalar event is a declaration of a new program, the key being
 * the program name
 */
DECL_YAML_HANDLER(yaml_scalar_2) {
  UNUSED_PARAM(parsing);
  t_pgm *new = calloc(1, sizeof(*new));
  if (!new) handle_error("calloc");
  if (node->head) new->privy.next = node->head;
  node->head = new;
  new->usr.name = strdup((char *)event->data.scalar.value);
  if (!new->usr.name) handle_error("strdup");
  return EXIT_SUCCESS;
}

static int8_t findkey(const char *key) {
  for (int8_t i = 1; i < KEY_NB_MAX; i++)
    if (!strcmp(keys[i], key)) return i;
  return (-1);
}

/* this depth of scalar event concerns all variables of a t_pgm */
DECL_YAML_HANDLER(yaml_scalar_3) {
  uint8_t ret = EXIT_SUCCESS;
  if (parsing->scalar_type == KEY_TYPE) {
    parsing->key = findkey((char *)event->data.scalar.value);
    ret = (ret * (parsing->key >= 0)) + (WRONG_KEY * (parsing->key == -1));
  } else if (parsing->scalar_type == VALUE_TYPE) {
    if (parsing->key < 1 || parsing->key >= KEY_NB_MAX) return EXIT_FAILURE;
    ret = handle_data_loading[parsing->key](&node->head->usr,
                                            (char *)event->data.scalar.value);
  } else
    return EXIT_FAILURE;
  if (!parsing->seq_depth)
    TOGGLE_TYPE(parsing->scalar_type); /* toggle between key & value */
  return ret;
}

/* this depth of scalar event should only be used for env values */
DECL_YAML_HANDLER(yaml_scalar_4) {
  uint8_t ret = EXIT_SUCCESS;

  if (parsing->key != KEY_ENV || parsing->key == -1 ||
      parsing->key >= KEY_NB_MAX)
    return EXIT_FAILURE;
  ret = handle_data_loading[parsing->key](&node->head->usr,
                                          (char *)event->data.scalar.value);
  return ret;
}

/* array of functions of type YAML_HANDLER */
uint8_t (*handle_yaml_scalar_event[YAML_MAX_SCALAR_EVENT])(t_tm_node *,
                                                           t_config_parsing *,
                                                           yaml_event_t *) = {
    yaml_scalar_0, yaml_scalar_1, yaml_scalar_2, yaml_scalar_3, yaml_scalar_4};

DECL_YAML_HANDLER(yaml_scalar) {
  UNUSED_PARAM(node);
  if (parsing->map_depth >= YAML_MAX_SCALAR_EVENT) return EXIT_FAILURE;
  return handle_yaml_scalar_event[parsing->map_depth](node, parsing, event);
}

DECL_YAML_HANDLER(yaml_seq_st) {
  UNUSED_PARAM(node);
  UNUSED_PARAM(event);
  if (parsing->map_depth < 3)
    return EXIT_FAILURE; /* no sequence before pgm definition */
  parsing->seq_depth++;
  return EXIT_SUCCESS;
}

DECL_YAML_HANDLER(yaml_seq_e) {
  UNUSED_PARAM(node);
  UNUSED_PARAM(event);
  parsing->scalar_type = KEY_TYPE; /* we fall back on a key after this event */
  parsing->seq_depth--;
  return EXIT_SUCCESS;
}

DECL_YAML_HANDLER(yaml_map_st) {
  UNUSED_PARAM(node);
  UNUSED_PARAM(event);
  parsing->map_depth++;
  return EXIT_SUCCESS;
}

DECL_YAML_HANDLER(yaml_map_e) {
  UNUSED_PARAM(node);
  UNUSED_PARAM(event);
  parsing->map_depth--;
  parsing->scalar_type = KEY_TYPE; /* we fall back on a key after this event */
  if (parsing->map_depth < 3) parsing->key = 0; /* reset key */
  return EXIT_SUCCESS;
}

/* array of functions of type YAML_HANDLER */
uint8_t (*handle_yaml_event[YAML_MAX_EVENT])(t_tm_node *, t_config_parsing *,
                                             yaml_event_t *) = {
    yaml_nothing, yaml_stream_st, yaml_stream_e, yaml_doc_st,
    yaml_doc_e,   yaml_alias,     yaml_scalar,   yaml_seq_st,
    yaml_seq_e,   yaml_map_st,    yaml_map_e};

uint8_t load_config_file(t_tm_node *node) {
  yaml_parser_t parser;
  yaml_event_t event;
  t_config_parsing parsing = {0};
  uint8_t done = 0, ret;

  yaml_parser_initialize(&parser);
  yaml_parser_set_input_file(&parser, node->config_file);

  /* Read the event sequence. */
  while (!done) {
    /* Get the next event. */
    if (!yaml_parser_parse(&parser, &event)) {
      if (parser.problem_mark.line || parser.problem_mark.column) {
        fprintf(stderr, "Parse error: %s\nLine: %lu Column: %lu\n",
                parser.problem, (unsigned long)parser.problem_mark.line + 1,
                (unsigned long)parser.problem_mark.column + 1);
      } else {
        fprintf(stderr, "Parse error: %s\n", parser.problem);
      }
      goto error;
    }

    ret = handle_yaml_event[event.type](node, &parsing, &event);
    if (ret) {
      handle_config_error(&event, ret, parsing.key);
      yaml_event_delete(&event);
      goto error;
    }
    done = (event.type == YAML_STREAM_END_EVENT);
    yaml_event_delete(&event);
  }

  yaml_parser_delete(&parser);
  return EXIT_SUCCESS;

error:
  destroy_pgm_list(&node->head);
  yaml_parser_delete(&parser);
  fclose(node->config_file);
  return EXIT_FAILURE;
}

uint8_t sanitize_config(t_tm_node *node) {
  t_pgm_usr *pgm;
  struct stat statbuf;
  t_keys key;
  uint8_t err, tot_err = 0;
  int8_t ret;

  for (t_pgm *head = node->head; head; head = head->privy.next) {
    pgm = &head->usr;
    err = 0;

    if (!pgm->cmd || !*(pgm->cmd))
      tot_err++, key = KEY_CMD, err = print_san_err(key, MISSING_ERROR, NULL);
    if (pgm->cmd) {
      ret = stat(pgm->cmd[0], &statbuf);
      if (ret == -1) {
        tot_err++, key = KEY_CMD, err = print_san_err(key, 0, strerror(errno));
      } else if (!ret) {
        if (!S_ISREG(statbuf.st_mode))
          tot_err++, key = KEY_CMD,
                     err = print_san_err(key, 0, "Not a regular file");
      }
    }
    if (pgm->workingdir) {
      ret = stat(pgm->workingdir, &statbuf);
      if (ret == -1) {
        tot_err++, key = KEY_WORKINGDIR,
                   err = print_san_err(key, 0, strerror(errno));
      } else if (!ret) {
        if (!S_ISDIR(statbuf.st_mode))
          tot_err++, key = KEY_WORKINGDIR,
                     err = print_san_err(key, 0, "Not a directory");
      }
    }
    if (!pgm->numprocs)
      tot_err++, key = KEY_NUMPROCS,
                 err = print_san_err(key, MISSING_ERROR, NULL);
    if (pgm->std_out) {
      head->privy.log.out =
          open(pgm->std_out, O_WRONLY | O_CREAT | O_APPEND, LOGFILE_PERM);
      if (head->privy.log.out == -1) {
        tot_err++, key = KEY_STDOUT,
                   err = print_san_err(key, 0, strerror(errno));
      }
    }
    if (pgm->std_err) {
      head->privy.log.err =
          open(pgm->std_err, O_WRONLY | O_CREAT | O_APPEND, LOGFILE_PERM);
      if (head->privy.log.err == -1) {
        tot_err++, key = KEY_STDERR,
                   err = print_san_err(key, 0, strerror(errno));
      }
    }
  }

  if (tot_err) {
    fprintf(stderr, "%d error%c detected\n", tot_err, tot_err > 1 ? 's' : '\0');
    destroy_taskmaster(node);
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
