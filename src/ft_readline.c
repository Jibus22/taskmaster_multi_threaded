/*
 * This non-canonical line reader does'nt uses termcap or termios database so
 * no additional library is linked to. In return this is only compatible with
 * terminal which understand VT100 sequences (the majority).
 * resources:
 * https://vt100.net/docs/vt100-ug/chapter3.html#CUF
 * https://www.gnu.org/software/libc/manual/html_node/Low_002dLevel-Terminal-Interface.html
 * https://github.com/antirez/linenoise
 */

#include "ft_readline.h"

/* Debugging macro. */
#if 0
FILE *rl_debug_fp = NULL;
#define rl_debug(...)                                                        \
  do {                                                                       \
    if (rl_debug_fp == NULL) {                                               \
      rl_debug_fp = fopen("/tmp/rl_debug.txt", "a");                         \
      fprintf(rl_debug_fp, "[%d %d %d] p: %d\n", (int)rl->len, (int)rl->pos, \
              (int)rl->oldpos, (int)rl->plen);                               \
    }                                                                        \
    fprintf(rl_debug_fp, ", " __VA_ARGS__);                                  \
    fflush(rl_debug_fp);                                                     \
  } while (0)

#define rl_debug2(...)                                                      \
  do {                                                                      \
    if (rl_debug_fp == NULL) rl_debug_fp = fopen("/tmp/rl_debug.txt", "a"); \
    fprintf(rl_debug_fp, ", " __VA_ARGS__);                                 \
    fflush(rl_debug_fp);                                                    \
  } while (0)
#else
#define rl_debug(fmt, ...)
#define rl_debug2(fmt, ...)
#endif

/* ============================== history =================================== */

static char *history[FT_READLINE_HISTORY_SZ];
static int32_t history_idx; /* head pointing after the last history record */
static uint32_t history_entries;

static void destroy_history() {
  for (uint32_t i = 0; i < history_entries && history[i]; i++) free(history[i]);
}

/* API function to add a new entry in the ft_readline history. This is
 * implemented as a circular buffer. Does'nt add empty line, space-filled line
 * or duplicate with previous row */
uint32_t ft_readline_add_history(const char *line) {
  int32_t i = 0;
  char *cpy;

  while (line[i] && line[i] == ' ') i++;
  if (!line[i]) return EXIT_FAILURE;
  if (history[CIRC_BUF_PREV(history_idx, FT_READLINE_HISTORY_SZ)] &&
      !strcmp(line,
              history[CIRC_BUF_PREV(history_idx, FT_READLINE_HISTORY_SZ)]))
    return EXIT_FAILURE;
  cpy = strdup(line);
  if (!cpy) return EXIT_FAILURE;
  if (history[history_idx]) free(history[history_idx]);
  history[history_idx] = cpy;
  history_idx = CIRC_BUF_NEXT(history_idx, FT_READLINE_HISTORY_SZ);
  history_entries += HISTORY_INC_ENTRY;
  return EXIT_SUCCESS;
}

/* ============================== terminal ================================== */

static struct termios orig_termios; /* In order to restore at exit.*/
static int atexit_registered = 0;   /* Register atexit just 1 time. */
static int rawmode = 0; /* For atexit() function to check if restore is needed*/

static void disable_raw_mode() {
  if (rawmode && tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios) != -1)
    rawmode = 0;
}

static void exit_ft_readline() {
  disable_raw_mode();
  destroy_history();
}

static uint8_t enable_raw_mode() {
  struct termios raw;

  if (!isatty(STDIN_FILENO)) goto fatal;
  if (!atexit_registered) {
    atexit(exit_ft_readline);
    atexit_registered = 1;
  }
  if (tcgetattr(STDIN_FILENO, &orig_termios) == -1) goto fatal;

  raw = orig_termios; /* modify the original mode */
  /* input modes: no break, no CR to NL, no parity check, no strip char,
   * no start/stop output control. */
  raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
  /* output modes - disable post processing */
  raw.c_oflag &= ~(OPOST);
  /* control modes - set 8 bit chars */
  raw.c_cflag |= (CS8);
  /* local modes - choing off, canonical off, no extended functions,
   * no signal chars (^Z,^C) */
  raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
  /* control chars - set return condition: min number of bytes and timer.
   * We want read to return every single byte, without timeout. */
  raw.c_cc[VMIN] = 1;
  raw.c_cc[VTIME] = 0; /* 1 byte, no timer */

  /* put terminal in raw mode after flushing */
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) < 0) goto fatal;
  rawmode = 1;
  return EXIT_SUCCESS;

fatal:
  errno = ENOTTY;
  return EXIT_FAILURE;
}

/* Use the ESC [6n escape sequence to query the horizontal cursor position
 * and return it. On error -1 is returned, on success the position of the
 * cursor. */
static int getCursorPosition(int ifd, int ofd) {
  char buf[32];
  int cols, rows;
  unsigned int i = 0;

  /* Report cursor location */
  if (write(ofd, "\x1b[6n", 4) != 4) return -1;

  /* Read the response: ESC [ rows ; cols R */
  while (i < sizeof(buf) - 1) {
    if (read(ifd, buf + i, 1) != 1) break;
    if (buf[i] == 'R') break;
    i++;
  }
  buf[i] = '\0';

  /* Parse it. */
  if (buf[0] != ESC || buf[1] != '[') return -1;
  if (sscanf(buf + 2, "%d;%d", &rows, &cols) != 2) return -1;
  return cols;
}

/* Try to get the number of columns in the current terminal, or assume 80
 * if it fails. */
static int getColumns(int ifd, int ofd) {
  struct winsize ws;

  if (ioctl(1, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
    /* ioctl() failed. Try to query the terminal itself. */
    int start, cols;

    /* Get the initial position so we can restore it later. */
    start = getCursorPosition(ifd, ofd);
    if (start == -1) goto failed;

    /* Go to right margin and get position. */
    if (write(ofd, "\x1b[999C", 6) != 6) goto failed;
    cols = getCursorPosition(ifd, ofd);
    if (cols == -1) goto failed;

    /* Restore position. */
    if (cols > start) {
      char seq[32];
      /* sequence (ESC [ nb D) is cursor backward with nb times to backward */
      snprintf(seq, 32, "\x1b[%dD", cols - start);
      if (write(ofd, seq, strlen(seq)) == -1) {
        /* Can't recover... */
      }
    }
    return cols;
  } else {
    return ws.ws_col;
  }

failed:
  return 80;
}

/* ============================= append buffer ============================== */

typedef struct abuf {
  char *b;
  int len;
} t_abuf;

static void ab_append(struct abuf *ab, const char *s, int len) {
  char *new = realloc(ab->b, ab->len + len);

  if (!new) return;
  memcpy(new + ab->len, s, len);
  ab->b = new;
  ab->len += len;
}

static void ab_free(struct abuf *ab) { free(ab->b); }

/* ============================== output ==================================== */

#define SEQ_BUF_SZ (64)

static void rl_refresh(t_readline_state *rl) {
  t_abuf ab = {NULL, 0};
  char seq[SEQ_BUF_SZ];
  size_t len_display = rl->len, shift = 0;

  if (rl->len + rl->plen >= rl->cols) len_display = rl->cols - rl->plen - 1;
  if (rl->pos > len_display) shift = rl->pos - len_display;
  if (rl->cols <= rl->plen) len_display = rl->cols;
  /* Cursor to left edge */
  snprintf(seq, sizeof(seq), "\r");
  ab_append(&ab, seq, strlen(seq));
  /* Write the prompt and the current buffer content */
  if (rl->cols > rl->plen) ab_append(&ab, rl->prompt, rl->plen);
  ab_append(&ab, rl->buf + shift, len_display);
  snprintf(seq, 64, "\x1b[0K"); /* Erase to right */
  ab_append(&ab, seq, strlen(seq));
  /* Move cursor to original position. */
  snprintf(seq, 64, "\r\x1b[%dC", (int)(rl->pos + rl->plen));
  ab_append(&ab, seq, strlen(seq));
  if (write(rl->ofd, ab.b, ab.len) == -1) {
  } /* Can't recover from write error. */
  ab_free(&ab);
}

static void rl_move_cursor_right(t_readline_state *rl) {
  if (rl->pos < rl->len) rl->pos++;
}

static void rl_move_cursor_left(t_readline_state *rl) {
  if (rl->pos > 0) rl->pos--;
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

/* Save current line and display previous command */
static void rl_prev_history_entry(t_readline_state *rl) {
  if (!history[CIRC_BUF_PREV(history_idx, FT_READLINE_HISTORY_SZ)]) return;
  if (!rl->init_hidx) {
    rl->history_index = history_idx;
    rl->init_hidx = 1;
  }
  if (!history[CIRC_BUF_PREV(rl->history_index, FT_READLINE_HISTORY_SZ)])
    rl->history_index = CIRC_BUF_PREV(history_idx, FT_READLINE_HISTORY_SZ);
  else
    rl->history_index =
        CIRC_BUF_PREV(rl->history_index, FT_READLINE_HISTORY_SZ);
  strcpy_safe(rl->buf, history[rl->history_index], rl->buflen);
  rl->len = rl->pos = strlen(rl->buf);
}

/* display next command */
static void rl_next_history_entry(t_readline_state *rl) {
  if (!rl->init_hidx || history_idx == rl->history_index) return;
  if (history_idx == CIRC_BUF_NEXT(rl->history_index, FT_READLINE_HISTORY_SZ)) {
    rl->history_index =
        CIRC_BUF_NEXT(rl->history_index, FT_READLINE_HISTORY_SZ);
    rl->buf[0] = rl->len = rl->pos = 0;
    return;
  }
  if (!history[CIRC_BUF_NEXT(rl->history_index, FT_READLINE_HISTORY_SZ)])
    return;
  rl->history_index = CIRC_BUF_NEXT(rl->history_index, FT_READLINE_HISTORY_SZ);
  strcpy_safe(rl->buf, history[rl->history_index], rl->buflen);
  rl->len = rl->pos = strlen(rl->buf);
}

/* =============================== completion =============================== */

/* return code to avoid 0 initialization of rl_compl_idx at each refresh */
#define RL_COMPLETION (4242)
static char **rl_compl = NULL;   /* completion strings copied from user call */
static int32_t rl_compl_sz = 0;  /* size of rl_compl */
static int32_t rl_compl_idx = 0; /* idx to rl_compl of the current completion */
static int32_t rl_compl_init = 0; /* is it the first tab press or not */

static void rl_beep(void) {
  fprintf(stderr, "\x7");
  fflush(stderr);
}

/* insert the matching word at the right position, in the current line buffer */
static void insert_completion(t_readline_state *rl, char *orig_buf,
                              int word_start, int word_end) {
  /* copy the leftover after the matching word */
  strcpy_safe(rl->buf + word_start + strlen(rl_compl[rl_compl_idx]),
              orig_buf + word_end, strlen(orig_buf + word_end) + 1);
  /* copy the matching word */
  strncpy(rl->buf + word_start, rl_compl[rl_compl_idx],
          strlen(rl_compl[rl_compl_idx]));
  rl->len = strlen(rl->buf);
  rl->pos = word_start + strlen(rl_compl[rl_compl_idx]);
  return;
}

/* Save buffer to complete and initialize addresses so that we can search many
 * matches against the current buffer */
static void rl_completion_init(t_readline_state *rl, char *orig_buf,
                               int32_t *word_start, int32_t *word_end,
                               int32_t *sz_cmp) {
  /* Save the original string to compare with following completions */
  strcpy_safe(orig_buf, rl->buf, rl->buflen);
  /* find where the word to compare starts */
  *word_start = *word_end = rl->pos;
  while (*word_start > 0 && orig_buf[*word_start - 1] != ' ') (*word_start)--;
  /* find where the word to compare ends */
  while (orig_buf[*word_end] && orig_buf[*word_end] != ' ') (*word_end)++;
  *sz_cmp = rl->pos - *word_start; /* compute how many characters to compare */
}

static int32_t circular_buf_next(int32_t idx, int32_t circular_buf_sz) {
  return (((idx) + 1) % (circular_buf_sz));
}

static int32_t circular_buf_prev(int32_t idx, int32_t circular_buf_sz) {
  return ((((idx) == 0) * ((circular_buf_sz)-1)) + (((idx) > 0) * ((idx)-1)));
}

typedef int32_t (*circular_buf_it)(int32_t idx, int32_t circular_buf_sz);

/* search for a match on the current word */
static void complete(t_readline_state *rl, char *orig_buf, int32_t *word_start,
                     int32_t *word_end, int32_t *sz_cmp,
                     circular_buf_it iterator) {
  int32_t idx_save = rl_compl_idx, match = 0, idx_first_match = 0;

  while (1) {
    rl_compl_idx = iterator(rl_compl_idx, rl_compl_sz);

    if (!strncmp(orig_buf + *word_start, rl_compl[rl_compl_idx], *sz_cmp)) {
      if ((int32_t)strlen(rl_compl[rl_compl_idx]) == *sz_cmp) return rl_beep();
      if (match) {
        /* second match: go back to the idx of first match and return */
        rl_compl_idx = idx_first_match;
        return;
      }
      /* first match: substitute completion, save idx and signal the match */
      insert_completion(rl, orig_buf, *word_start, *word_end);
      idx_first_match = rl_compl_idx;
      match++;
    }

    if (rl_compl_idx == idx_save) {
      if (match) /* only one match. Comparison will stop here on further tab */
        rl_completion_init(rl, orig_buf, word_start, word_end, sz_cmp);
      else /* no match at all */
        rl_beep();
      return;
    }
  }
}

static void rl_completion(t_readline_state *rl, circular_buf_it iterator) {
  static char orig_buf[FT_READLINE_MAX_LINE];
  static int32_t word_start, sz_cmp, word_end;

  if (!rl_compl_sz) return rl_beep();

  if (!rl_compl_init)
    rl_completion_init(rl, orig_buf, &word_start, &word_end, &sz_cmp);
  if (!sz_cmp) return rl_beep();

  complete(rl, orig_buf, &word_start, &word_end, &sz_cmp, iterator);
}

static void destroy_completion() {
  if (!rl_compl) return;
  for (int32_t i = 0; i < rl_compl_sz; i++) {
    free(rl_compl[i]);
    rl_compl[i] = NULL;
  }
  free(rl_compl);
  rl_compl = NULL;
}

/* ft_readline API function to add commands to completion engine.
 * Can be used only once. Copies cmds ptr into rl_compl.
 * cmds must be dynamically allocated and not free by the user.
 * Implemented as a circular buffer.
 * if success return 0, non-zero otherwise
 *
 * Should be improved later to sort the array */
int32_t ft_readline_add_completion(char **cmds, size_t nb) {
  if (rl_compl || !nb || !cmds) return EXIT_FAILURE;
  rl_compl_sz = nb;
  rl_compl = cmds;
  atexit(destroy_completion);
  return EXIT_SUCCESS;
}

/* ============================== line editing ============================== */

static void rl_delete_line(t_readline_state *rl) {
  rl->buf[0] = rl->pos = rl->len = 0;
}

static void rl_insert_char(t_readline_state *rl, int c) {
  if (rl->len == rl->buflen) return;
  if (rl->pos < rl->len)
    memmove(rl->buf + rl->pos + 1, rl->buf + rl->pos, rl->len - rl->pos);
  rl->buf[rl->pos] = c;
  rl->pos++;
  rl->len++;
  rl->buf[rl->len] = 0;
}

static void rl_delete_char(t_readline_state *rl) {
  if (!rl->pos) return;
  memmove(rl->buf + rl->pos - 1, rl->buf + rl->pos, rl->len - rl->pos);
  rl->pos--;
  rl->len--;
  rl->buf[rl->len] = 0;
}

/* ============================== input ===================================== */

static int32_t rl_read_key() {
  int nread;
  char c;

  nread = read(STDIN_FILENO, &c, 1);
  if (nread <= 0) return -4242;

  rl_debug2("c: %d", c);

  /* '\x1b' is hexa for 27 aka escape ascii */
  if (c == '\x1b') {
    char seq[3];
    if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
    if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

    /* ESC [ sequences. */
    if (seq[0] == '[') {
      if (seq[1] >= '0' && seq[1] <= '9') {
        /* Extended escape, read additional byte. */
        if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
        if (seq[2] == '~') {
          switch (seq[1]) {
            case '1':
              return HOME_KEY;
            case '3':
              return DEL_KEY;
            case '4':
              return END_KEY;
            case '5':
              return PAGE_UP;
            case '6':
              return PAGE_DOWN;
            case '7':
              return HOME_KEY;
            case '8':
              return END_KEY;
          }
        }
      } else {
        switch (seq[1]) {
          case 'A':
            return ARROW_UP;
          case 'B':
            return ARROW_DOWN;
          case 'C':
            return ARROW_RIGHT;
          case 'D':
            return ARROW_LEFT;
          case 'H':
            return HOME_KEY;
          case 'F':
            return END_KEY;
          case 'Z':
            return SHIFT_TAB;
        }
      }
    } else if (seq[0] == 'O') {
      switch (seq[1]) {
        case 'H':
          return HOME_KEY;
        case 'F':
          return END_KEY;
      }
    }
    return '\x1b';
  } else {
    return c;
  }
}

static int32_t rl_process_key(t_readline_state *rl) {
  int32_t c = rl_read_key();

  if (c == -4242) goto rl_exit;
  if (c < 0) return 1;

  switch (c) {
    case ENTER:
      goto rl_return;

    case CTRL_KEY('q'):
      goto rl_exit;
      break;

    case CTRL_KEY('c'):
      /* TODO */
      break;
    case CTRL_KEY('z'):
      raise(SIGTSTP);
      break;
    case CTRL_KEY('d'):
      /* TODO */
      break;

    case CTRL_KEY('i'): /* TAB (9) */
      rl_completion(rl, circular_buf_next);
      goto rl_completion;
    case SHIFT_TAB:
      rl_completion(rl, circular_buf_prev);
      goto rl_completion;

    case CTRL_KEY('a'):
      rl->pos = 0;
      break;
    case CTRL_KEY('e'):
      rl->pos = rl->len;
      break;
    case CTRL_KEY('u'):
      rl_delete_line(rl);
      break;

    case CTRL_KEY('g'):
    case CTRL_KEY('j'):
    case CTRL_KEY('k'):
    case CTRL_KEY('o'):
    case CTRL_KEY('r'):
    case CTRL_KEY('s'):
    case CTRL_KEY('t'):
    case CTRL_KEY('v'):
    case CTRL_KEY('w'):
    case CTRL_KEY('x'):
    case CTRL_KEY('y'):
      break;

    case PAGE_UP:
    case PAGE_DOWN:
    case HOME_KEY:
    case END_KEY:
      break;

    case BACKSPACE:
    case CTRL_KEY('h'):
    case DEL_KEY:
      rl_delete_char(rl);
      break;

    case CTRL_KEY('n'):
    case ARROW_DOWN:
      rl_next_history_entry(rl);
      break;
    case CTRL_KEY('p'):
    case ARROW_UP:
      rl_prev_history_entry(rl);
      break;

    case CTRL_KEY('b'):
    case ARROW_LEFT:
      rl_move_cursor_left(rl);
      break;
    case CTRL_KEY('f'):
    case ARROW_RIGHT:
      rl_move_cursor_right(rl);
      break;

    case CTRL_KEY('l'):
    case '\x1b':
      break;

    default:
      rl_insert_char(rl, c);
      break;
  }
  return 1;
rl_completion:
  return RL_COMPLETION;
rl_return:
  return 0;
rl_exit:
  return -1;
}

/* ================================ init ==================================== */

static void rl_init(t_readline_state *rl, char *buf, const char *prompt,
                    size_t buflen) {
  rl->ifd = STDIN_FILENO;
  rl->ofd = STDOUT_FILENO;
  rl->buf = buf;
  rl->buflen = buflen - 1; /* Make sure there is always space for the nulterm */
  rl->prompt = prompt;
  rl->plen = strlen(prompt);
  rl->pos = 0;
  rl->len = 0;
  rl->cols = getColumns(STDIN_FILENO, STDOUT_FILENO);
  rl->history_index = 0;
  rl->init_hidx = 0;
  rl->buf[0] = '\0'; /* Buffer starts empty. */
}

/* Main API function. Takes non NULL C-string as argument. Gives a prompt with
 * completion and history facilities. Returns a dynamically allocated edited
 * line. The user must free it */
char *ft_readline(const char *prompt) {
  char buf[FT_READLINE_MAX_LINE];
  t_readline_state rl;
  int32_t run = 1;

  assert(prompt);
  if (enable_raw_mode()) return NULL;
  rl_init(&rl, buf, prompt, FT_READLINE_MAX_LINE);

  if (write(rl.ofd, prompt, rl.plen) == -1) return NULL;
  while (run > 0) {
    rl_refresh(&rl);
    run = rl_process_key(&rl);
    rl_compl_init = (run == RL_COMPLETION);
  }

  disable_raw_mode();
  printf("\n");
  if (run < 0) return NULL;
  return strdup(buf);
}
