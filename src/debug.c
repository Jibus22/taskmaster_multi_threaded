#include "taskmaster.h"

void print_pgm_list(t_pgm *pgm) {
    while (pgm) {
        printf("-------------------\n");
        printf("addr: %p\nname: %s\nstdout: %s\nstderr: %s\nworkingdir: %s\n",
               pgm, pgm->name, pgm->std_out, pgm->std_err, pgm->workingdir);
        printf("cmd:\n");
        for (uint32_t i = 0; pgm->cmd && pgm->cmd[i]; i++)
            printf("\t%s\n", pgm->cmd[i]);
        printf("env:\n");
        for (uint32_t i = 0; pgm->env.array_val && pgm->env.array_val[i]; i++)
            printf("\t%s\n", pgm->env.array_val[i]);
        printf("exitcodes:\n");
        for (uint32_t i = 0; i < pgm->exitcodes.array_size; i++)
            printf("\t%d\n", pgm->exitcodes.array_val[i]);
        printf(
            "numprocs: %d\numask: %o\nautorestart: %d\nstartretries: "
            "%d\nautostart: %d\nstopsignal: %s\nstarttime: %d\nstoptime: "
            "%d\nnext: %p\n",
            pgm->numprocs, pgm->umask, pgm->autorestart, pgm->startretries,
            pgm->autostart, pgm->stopsignal.name, pgm->starttime, pgm->stoptime,
            pgm->next);
        fflush(stdout);
        pgm = pgm->next;
    }
}
