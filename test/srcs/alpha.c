#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>

#define BUF_SIZE 128

#ifndef DAEMON_NAME
#define DAEMON_NAME "ALPHA"
#endif

#ifndef SLEEP_TIME
#define SLEEP_TIME 8
#endif

void handler(int num) {
  fprintf(stdout, "signal %d catched\n", num);
  fflush(stdout);
  fprintf(stderr, "signal %d catched\n", num);
  fflush(stderr);
}

int main(int ac, char **av, char **env) {
  pid_t pid = getpid();
  char msg_out[BUF_SIZE] = {0};
  char msg_err[BUF_SIZE] = {0};
  int len_out = sprintf(msg_out, "[%-6d] - %-8s - STDOUT - RUNNING\n", pid,
			DAEMON_NAME),
      len_err = sprintf(msg_err, "[%-6d] - %-8s - STDERR - RUNNING\n", pid,
			DAEMON_NAME);

  signal(SIGTERM, handler);

  write(STDOUT_FILENO, msg_out, len_out);
  write(STDERR_FILENO, msg_err, len_err);
  if (env) {
    printf("=== %-6s [%-5d] environment: ===\n", DAEMON_NAME, pid);
    for (int i = 0; env[i]; i++)
      printf("%s\n", env[i]);
  }
  if (av) {
    printf("=== %-6s [%-5d] argv: ===\n", DAEMON_NAME, pid);
    for (int i = 0; av[i]; i++)
      printf("%s\n", av[i]);
  }
  fflush(stdout);
  while (1) {
    sleep(SLEEP_TIME);
    write(STDOUT_FILENO, msg_out, len_out);
    write(STDERR_FILENO, msg_err, len_err);
  }
  return EXIT_SUCCESS;
}
