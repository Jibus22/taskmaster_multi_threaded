#ifndef FT_READLINE_H
#define FT_READLINE_H

#include <inttypes.h>
#include <unistd.h>

#define FT_READLINE_MAX_LINE (4096)

/* 0x1f / 31 : mask of 00011111: 3 last bits aren't compared and we keep
 * the 5 first. Key command ctrl-[a-z-(specials)] go from 1 to 31 so this
 * macro permits us to directly fetch the command associated with its char */
#define CTRL_KEY(k) ((k)&0x1f)

enum readline_key {
  KEY_NULL = 0,    /* NULL */
  TAB = 9,         /* Tab */
  ENTER = 13,      /* Enter */
  ESC = 27,        /* Escape */
  BACKSPACE = 127, /* Backspace */
  ARROW_LEFT = 1000,
  ARROW_RIGHT,
  ARROW_UP,
  ARROW_DOWN,
  HOME_KEY,
  END_KEY,
  PAGE_UP,
  PAGE_DOWN,
  DEL_KEY,
  SHIFT_TAB
};

/* The readline_state structure represents the state during line editing.
 * We pass this state to functions implementing specific editing
 * functionalities. */
typedef struct readline_state {
  int32_t ifd;           /* Terminal stdin file descriptor. */
  int32_t ofd;           /* Terminal stdout file descriptor. */
  char *buf;             /* Edited line buffer. */
  size_t buflen;         /* Edited line buffer size. */
  const char *prompt;    /* Prompt to display. */
  size_t plen;           /* Prompt length. */
  size_t pos;            /* Current cursor position. */
  size_t len;            /* Current edited line length. */
  size_t cols;           /* Number of columns in terminal. */
  int32_t history_index; /* The history index we are currently editing. */
  int32_t init_hidx;     /* Init history_index */
} t_readline_state;

/* circular buffer iterators */
#define CIRC_BUF_NEXT(idx, circular_buf_sz) (((idx) + 1) % (circular_buf_sz))
#define CIRC_BUF_PREV(idx, circular_buf_sz) \
  ((((idx) == 0) * ((circular_buf_sz)-1)) + (((idx) > 0) * ((idx)-1)))

/* history */
#define FT_READLINE_HISTORY_SZ (50)
#define HISTORY_INC_ENTRY (1 * (history_entries < FT_READLINE_HISTORY_SZ))

char *ft_readline(const char *prompt);
int32_t ft_readline_add_completion(char **cmds, size_t nb);
uint32_t ft_readline_add_history(const char *line);

#endif
