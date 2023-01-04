#include "run_server.h"

#include <pthread.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>

/*================================= getters ==================================*/

THRD_DATA_GET_IMPLEMENTATION(uint32_t)
THRD_DATA_GET_IMPLEMENTATION(int32_t)
THRD_DATA_GET_IMPLEMENTATION(pid_t)
THRD_DATA_GET_IMPLEMENTATION(pthread_t)
THRD_DATA_GET_IMPLEMENTATION(tm_timeval_t)

PGM_SPEC_GET_IMPLEMENTATION(uint32_t)
PGM_SPEC_GET_IMPLEMENTATION(int32_Ptr)
PGM_SPEC_GET_IMPLEMENTATION(uint8_t)
PGM_SPEC_GET_IMPLEMENTATION(bool)
PGM_SPEC_GET_IMPLEMENTATION(t_autorestart)
PGM_SPEC_GET_IMPLEMENTATION(char_Ptr)
PGM_SPEC_GET_IMPLEMENTATION(tm_timeval_t)

/*=================================== utils ==================================*/

/* time diff in ms */
static uint32_t timediff(struct timeval *time1) {
    struct timeval time2;

    gettimeofday(&time2, NULL);
    return (((time2.tv_sec - time1->tv_sec) * 1000000) +
            (time2.tv_usec - time1->tv_usec)) /
           1000;
}

static uint32_t timediff2(struct timeval time1) {
    struct timeval time2;

    gettimeofday(&time2, NULL);
    return (((time2.tv_sec - time1.tv_sec) * 1000000) +
            (time2.tv_usec - time1.tv_usec)) /
           1000;
}

/*================================ timer thread ==============================*/

/* checks if the process is stopped in the given time otherwise it sends a kill
 * signal. To be sure the signal is catched, it is sent in a loop during 1 sec
 * if ever the proc is still alive. */
static uint8_t stop_time(t_thread_data *thrd) {
    struct timeval stop;

    /* in the case of a processus stopping without client event, stopped state
     * is set directly after the waitpid() and we don't want to time it */
    if (GET_PROC_STATE == PROC_ST_STOPPED) return EXIT_SUCCESS;

    SET_PROC_STATE(PROC_ST_STOPPING);
    pthread_mutex_unlock(&thrd->mtx_timer);
    sem_wait(&thrd->sync); /* sync with stop_signal() */
    pthread_mutex_lock(&thrd->mtx_timer);
    while ((THRD_DATA_GET(pid_t, pid) > 0) &&
           timediff2((PGM_SPEC_GET_T(tm_timeval_t, privy.stop_timestamp))) <
               PGM_SPEC_GET_T(uint32_t, usr.stoptime))
        usleep(STOP_SUPERVISOR_RATE);

    gettimeofday(&stop, NULL);
    if (THRD_DATA_GET(pid_t, pid)) {
        THRD_DATA_SET(restart_counter, 0);
        while (THRD_DATA_GET(pid_t, pid) && timediff(&stop) < KILL_TIME_LIMIT) {
            kill(THRD_DATA_GET(pid_t, pid), SIGKILL);
            usleep(STOP_SUPERVISOR_RATE);
        }
        if (timediff(&stop) > KILL_TIME_LIMIT) {
            TM_STOP_LOG("ERR: TASKMASTER DIDN'T SUCCEEDED TO KILL THE PROC");
        } else
            TM_STOP_LOG("PROCESSUS HAD BEEN KILLED");
    } else
        TM_STOP_LOG("PROCESSUS STOPPED AS EXPECTED");

    SET_PROC_STATE(PROC_ST_STOPPED);
    return EXIT_SUCCESS;
}

/* This joinable thread is a timer which is coupled with its launcher thread.
 * It checks if the processus is launched correctly, then wait for a restart or
 * an exit, to count the stop time. */
static void *timer(void *arg) {
    t_thread_data *thrd = arg;
    struct timeval started;
    bool init = false;
    pid_t pid;

idle_timer:
    pthread_barrier_wait(&thrd->sync_barrier);
    pthread_mutex_lock(&thrd->mtx_wakeup);
    if (!init) {
        init = true;
        /* sync thread_pool creation with start*/
        sem_post(&thrd->sync);
    }
    /* Waits for MT to signal a start */
    pthread_cond_wait(&thrd->cond_wakeup, &thrd->mtx_wakeup);
    pthread_mutex_unlock(&thrd->mtx_wakeup);
    if (GET_THRD_EVENT == THRD_EV_EXIT) return NULL;
start_timer:
    pthread_barrier_wait(&thrd->sync_barrier);
    pthread_mutex_lock(&thrd->mtx_timer);
    SET_PROC_STATE(PROC_ST_STARTING); /* Careful: this clears thread_event */
    sem_post(&thrd->sync); /* sync launcher thread with timer at init */
wait:
    if (GET_THRD_EVENT) {
        stop_time(thrd);
        goto check_ret;
    }

    pthread_cond_wait(&thrd->cond_timer, &thrd->mtx_timer);

    if (GET_THRD_EVENT) {
        stop_time(thrd);
        goto check_ret;
    }

    started = THRD_DATA_GET(tm_timeval_t, start_timestamp);
    pid = THRD_DATA_GET(pid_t, pid);

    SET_PROC_STATE(PROC_ST_STARTING);
    while (timediff(&started) < PGM_SPEC_GET_T(uint32_t, usr.starttime)) {
        if (GET_THRD_EVENT) {
            TM_START_LOG("EXITED BEFORE TIME TO LAUNCH");
            stop_time(thrd);
            goto check_ret;
        }
        if (GET_PROC_STATE == PROC_ST_STOPPED) {
            TM_START_LOG("DIDN'T STARTED CORRECTLY");
            goto wait;
        }
        usleep(START_SUPERVISOR_RATE);
    }
    SET_PROC_STATE(PROC_ST_STARTED);

    TM_START_LOG("STARTED CORRECTLY");
    goto wait;

check_ret:
    if (GET_THRD_EVENT == THRD_EV_NOEVENT || GET_THRD_EVENT == THRD_EV_STOP) {
        pthread_mutex_unlock(&thrd->mtx_timer);
        goto idle_timer;
    } else if (GET_THRD_EVENT == THRD_EV_RESTART) {
        pthread_mutex_unlock(&thrd->mtx_timer);
        goto start_timer;
    }
    pthread_mutex_unlock(&thrd->mtx_timer);
    return NULL;
}

/*============================= launcher thread ==============================*/

/* Set pid & tid of the related thread_data structure to 0 */
static void exit_thread(t_thread_data *thrd) {
    if (!thrd) return;
    THRD_DATA_SET(pid, 0);
    /* thrd->pgm->privy.nb_thread_alive--; */
}

static void *exit_launcher_thread(t_thread_data *thrd) {
    if (pthread_join(THRD_DATA_GET(pthread_t, timer_id), NULL))
        perror("pthread_join");
    exit_thread(thrd);
    TM_THRD_LOG("EXITED");
    return NULL;
}

/* wait for child to exit() or to be killed by any signal */
static int32_t child_control(t_thread_data *thrd, pid_t pid) {
    int32_t w, wstatus, child_ret = 0;
    uint8_t expected = false;

    do {
        w = waitpid(pid, &wstatus, WUNTRACED | WCONTINUED);
        if (w == -1) handle_error("waitpid");
        if (WIFEXITED(wstatus)) {
            child_ret = WEXITSTATUS(wstatus);
            for (uint16_t i = 0;
                 !expected &&
                 i < PGM_SPEC_GET_T(uint32_t, usr.exitcodes.array_size);
                 i++)
                expected =
                    child_ret ==
                    PGM_SPEC_GET_T(int32_Ptr, usr.exitcodes.array_val)[i];
            if (expected) {
                if (PGM_SPEC_GET_T(t_autorestart, usr.autorestart) ==
                    autorestart_unexpected)
                    THRD_DATA_SET(restart_counter, 0);
                TM_CHILDCONTROL_LOG("EXITED WITH EXPECTED STATUS");
            } else
                TM_CHILDCONTROL_LOG("EXITED WITH UNEXPECTED STATUS");
        } else if (WIFSIGNALED(wstatus)) {
            child_ret = WTERMSIG(wstatus);
            TM_CHILDCONTROL_LOG("KILLED BY SIGNAL");
        } else if (WIFSTOPPED(wstatus)) {
            child_ret = WSTOPSIG(wstatus);
            TM_CHILDCONTROL_LOG("STOPPED BY SIGNAL");
        } else if (WIFCONTINUED(wstatus)) {
            TM_CHILDCONTROL_LOG("CONTINUED");
        }
    } while (!WIFEXITED(wstatus) && !WIFSIGNALED(wstatus));
    THRD_DATA_SET(pid, 0);
    if (GET_THRD_EVENT == THRD_EV_NOEVENT) SET_PROC_STATE(PROC_ST_STOPPED);
    return child_ret;
}

/* Sets configuration asked from config file like umask, working directory
 * or file logging then execve() the process.
 *
 * As it is a child from a multi-threaded program, only uses async-signal-safe
 * functions.
 * However valgrind drd still triggers errors. Errors are relative to some sort
 * of race condition related to my locks (as rw_lock mutexes for example) but
 * as the child acts in a new memory area and doesn't have any concurrent data
 * access with its parent I don't understand the issue. No errors are triggered
 * when using vfork() instead but its manual says to not use any functions
 * before execve() so this isn't usable in our case. Can't figure out how
 * to fix that now. Hopefully this is a school pgm plus a poc. Final design will
 * be async instead, however the question remains. */
static void configure_and_launch(t_thread_data *thrd) {
    t_pgm *pgm = thrd->pgm;

    if (pgm->usr.umask) umask(pgm->usr.umask); /* default file mode creation */
    if (pgm->usr.workingdir) {
        if (chdir((char *)pgm->usr.workingdir) == -1) perror("chdir");
    }
    dup2(pgm->privy.log.out, STDOUT_FILENO);
    dup2(pgm->privy.log.err, STDERR_FILENO);
    if (execve(pgm->usr.cmd[0], pgm->usr.cmd,
               (char **)pgm->usr.env.array_val) == -1)
        perror("execve");
    exit(EXIT_FAILURE);
}

/* Update information of the thread_data struct related to one process - the
 * timestamp, pid & restart_counter.
 * Launch an attached timer thread which monitor if the process is still alive
 * after 'start_time' seconds. */
static void thread_data_update(t_thread_data *thrd, pid_t pid) {
    int32_t rt = THRD_DATA_GET(int32_t, restart_counter) - 1;
    struct timeval start;

    /* if the stop timer didn't finished, wait it */
    pthread_mutex_lock(&thrd->mtx_timer);
    gettimeofday(&start, NULL);
    THRD_DATA_SET(start_timestamp, start);
    THRD_DATA_SET(pid, pid);
    THRD_DATA_SET(restart_counter, rt);
    pthread_cond_signal(&thrd->cond_timer); /* start the start timer */
    pthread_mutex_unlock(&thrd->mtx_timer);
    TM_THRD_LOG("LAUNCHED");
    /* debug_thrd(); */
}

static void *run_process(t_thread_data *thrd) {
    int32_t pgm_restart = 1;
    pid_t pid;

    THRD_DATA_SET(restart_counter,
                  PGM_SPEC_GET_T(uint8_t, usr.startretries) + 1);
    while (pgm_restart > 0) {
        /* the more it restarts the more it sleeps (supervisord behavior) */
        sleep((PGM_SPEC_GET_T(uint8_t, usr.startretries) + 1) -
              THRD_DATA_GET(int32_t, restart_counter));

        if (GET_THRD_EVENT) break;
        pid = fork();
        if (pid == -1) {
            handle_error("fork");
            return NULL;
        }
        if (pid == 0)
            configure_and_launch(thrd);
        else {
            thread_data_update(thrd, pid);
            child_control(thrd, pid);
            pgm_restart = PGM_SPEC_GET_T(t_autorestart, usr.autorestart) *
                          (THRD_DATA_GET(int32_t, restart_counter));
            if (GET_THRD_EVENT == THRD_EV_NOEVENT && pgm_restart)
                TM_LOG("auto restart", "", NULL);
        }
    }
    return NULL;
}

/*
 * Routine of a launcher_thread.
 * The thread must first find its id to match with the t_thread_data
 * it is allowed to write/read in.
 * Redirect stdout and stderr of child if necessary before execve()
 * Responsible for relaunching the program if required.
 * @args:
 *   void *arg  is the address of one t_pgm which
 *              contains the configuration of the related program with, among
 *              other things, array of t_thread_data.
 **/
static void *routine_launcher_thrd(void *arg) {
    t_thread_data *thrd = arg;
    bool init = false;

    /* thrd->pgm->privy.nb_thread_alive++; */
    if (pthread_create(&thrd->timer_id, NULL, timer, arg)) {
        handle_error("pthread_create");
        return NULL;
    }
idle_launcher:
    pthread_barrier_wait(&thrd->sync_barrier);
    pthread_mutex_lock(&thrd->mtx_wakeup);
    if (!init) {
        init = true;
        /* sync thread_pool creation with start*/
        sem_post(&thrd->sync);
    }
    /* Waits for MT to signal a start */
    pthread_cond_wait(&thrd->cond_wakeup, &thrd->mtx_wakeup);
    pthread_mutex_unlock(&thrd->mtx_wakeup);
    if (GET_THRD_EVENT == THRD_EV_EXIT) return exit_launcher_thread(thrd);
start_launcher:
    pthread_barrier_wait(&thrd->sync_barrier);
    sem_wait(&thrd->sync);
    /* thrd_event may have changed in the meantime so we need to check it */
    if (GET_THRD_EVENT == THRD_EV_EXIT) return exit_launcher_thread(thrd);

    run_process(thrd);

    pthread_mutex_lock(&thrd->mtx_timer); /* wait stop timer to finish */
    if (GET_THRD_EVENT == THRD_EV_NOEVENT) {
        SET_THRD_EVENT(THRD_EV_STOP);
        sem_post(&thrd->sync);
        pthread_cond_signal(&thrd->cond_timer);
        pthread_mutex_unlock(&thrd->mtx_timer);
        goto idle_launcher;
    } else if (GET_THRD_EVENT == THRD_EV_STOP) {
        pthread_mutex_unlock(&thrd->mtx_timer);
        goto idle_launcher;
    } else if (GET_THRD_EVENT == THRD_EV_RESTART) {
        pthread_mutex_unlock(&thrd->mtx_timer);
        goto start_launcher;
    } else { /* PROC_EV_EXITING */
        pthread_mutex_unlock(&thrd->mtx_timer);
        return exit_launcher_thread(thrd);
    }
}

/*============================== handlers utils ==============================*/

static void stop_signal(t_thread_data *thrd, int32_t signal) {
    THRD_DATA_SET(restart_counter, 0);
    if (THRD_DATA_GET(pthread_t, tid) && GET_PROC_STATE != PROC_ST_STOPPING) {
        /* we signal the timer here because the kill above can miss or take
         * long time so the launcher_thread stay blocked and doesn't return
         * neither exit. The timer is responsible for kill with SIGTERM if
         * it takes too much time. */
        pthread_mutex_lock(&thrd->mtx_timer);
        sem_post(&thrd->sync);
        pthread_cond_signal(&thrd->cond_timer);
        if (THRD_DATA_GET(pid_t, pid)) kill(THRD_DATA_GET(pid_t, pid), signal);
        pthread_mutex_unlock(&thrd->mtx_timer);
    }
}

static uint8_t exit_pgm_launchers(t_pgm *pgm) {
    t_thread_data *thrd;
    struct timeval stop;

    gettimeofday(&stop, NULL);
    PGM_SPEC_SET(privy.stop_timestamp, stop);
    for (uint32_t id = 0; id < pgm->usr.numprocs; id++) {
        thrd = &pgm->privy.thrd[id];
        SET_THRD_EVENT(THRD_EV_EXIT);
        stop_signal(thrd, PGM_SPEC_GET_T(uint8_t, usr.stopsignal.nb));

        if (THRD_DATA_GET(pthread_t, tid) && !IS_PROC_ACTIVE(thrd)) {
            pthread_mutex_lock(&thrd->mtx_wakeup);
            pthread_cond_broadcast(&thrd->cond_wakeup);
            pthread_mutex_unlock(&thrd->mtx_wakeup);
        }
    }
    return EXIT_SUCCESS;
}

static uint8_t join_pgm_launchers(t_pgm *pgm) {
    t_thread_data *thrd;

    for (uint32_t id = 0; id < pgm->usr.numprocs; id++) {
        thrd = &pgm->privy.thrd[id];
        if (pthread_join(THRD_DATA_GET(pthread_t, tid), NULL))
            perror("pthread_join");
    }
    return EXIT_SUCCESS;
}

/* Create a pool of launcher thread for pgm */
static uint8_t create_launcher_pool(t_pgm *pgm) {
    t_thread_data *thrd;
    for (uint32_t id = 0; id < pgm->usr.numprocs; id++) {
        thrd = &pgm->privy.thrd[id];
        if (pthread_create(&thrd->tid, NULL, routine_launcher_thrd, thrd))
            handle_error("pthread_create");

        /* waits that LT & its timer are idle and waiting for start signal*/
        sem_wait(&thrd->sync);
        sem_wait(&thrd->sync);
    }
    return EXIT_SUCCESS;
}

/*============================== event handlers ==============================*/

/* generic declaration for command handlers */
#define DECL_EV_HANDLER(name) static uint8_t name(t_pgm *pgm, t_tm_node *node)

DECL_EV_HANDLER(do_status) {
    if (pgm)
        TM_LOG2("status", "%s", PGM_SPEC_GET(char_Ptr, usr.name));
    else
        TM_LOG2("status", "", NULL);
    UNUSED_PARAM(pgm);
    UNUSED_PARAM(node);
    return EXIT_SUCCESS;
}

/* Start launcher_threads from one t_pgm, in a
 * detached mode, if it doesn't exists yet and if is inactive.
 * If the thread already exists the restart_counter is still reset. */
DECL_EV_HANDLER(do_start) {
    t_thread_data *thrd;

    TM_LOG2("start", "%s", PGM_SPEC_GET(char_Ptr, usr.name));
    for (uint32_t id = 0; id < pgm->usr.numprocs; id++) {
        thrd = &pgm->privy.thrd[id];

        if (THRD_DATA_GET(pthread_t, tid) && !IS_PROC_ACTIVE(thrd)) {
            pthread_mutex_lock(&thrd->mtx_wakeup);
            /* signal LT and its timer to start workflow */
            pthread_cond_broadcast(&thrd->cond_wakeup);
            pthread_mutex_unlock(&thrd->mtx_wakeup);
        }
    }
    return EXIT_SUCCESS;
}

/* Stops all processus from one t_pgm with the signal
 * set in configuration file.
 * Does nothing if the thread is already down or is stopping. */
DECL_EV_HANDLER(do_stop) {
    t_thread_data *thrd;
    struct timeval stop;

    gettimeofday(&stop, NULL);
    PGM_SPEC_SET(privy.stop_timestamp, stop);
    TM_LOG2("stop", "%s", PGM_SPEC_GET(char_Ptr, usr.name));
    for (uint32_t id = 0; id < pgm->usr.numprocs; id++) {
        thrd = &pgm->privy.thrd[id];
        SET_THRD_EVENT(THRD_EV_STOP);
        stop_signal(thrd, pgm->usr.stopsignal.nb);
    }
    return EXIT_SUCCESS;
}

DECL_EV_HANDLER(do_restart) {
    t_thread_data *thrd;
    struct timeval stop;

    gettimeofday(&stop, NULL);
    PGM_SPEC_SET(privy.stop_timestamp, stop);
    TM_LOG2("restart", "%s", PGM_SPEC_GET(char_Ptr, usr.name));
    for (uint32_t id = 0; id < PGM_SPEC_GET(uint32_t, usr.numprocs); id++) {
        thrd = &pgm->privy.thrd[id];
        SET_THRD_EVENT(THRD_EV_RESTART);
        stop_signal(thrd, pgm->usr.stopsignal.nb);

        if (THRD_DATA_GET(pthread_t, tid) && !IS_PROC_ACTIVE(thrd)) {
            pthread_mutex_lock(&thrd->mtx_wakeup);
            pthread_cond_broadcast(&thrd->cond_wakeup);
            pthread_mutex_unlock(&thrd->mtx_wakeup);
        }
    }
    return EXIT_SUCCESS;
}

/* exit and destroy pgm. This function is blocking as it joins launchers */
DECL_EV_HANDLER(do_del) {
    TM_LOG2("delete", "%s", PGM_SPEC_GET(char_Ptr, usr.name));
    if (exit_pgm_launchers(pgm)) return EXIT_FAILURE;
    if (join_pgm_launchers(pgm)) return EXIT_FAILURE;

    destroy_pgm(pgm);
    free(pgm);

    return EXIT_SUCCESS;
}

/* create launchers of pgm and start them if auto_start is true */
DECL_EV_HANDLER(do_add) {
    TM_LOG2("add", "%s", PGM_SPEC_GET(char_Ptr, usr.name));
    if (create_launcher_pool(pgm)) return EXIT_FAILURE;

    if (PGM_SPEC_GET(bool, usr.autostart))
        if (do_start(pgm, node)) return EXIT_FAILURE;
    return EXIT_SUCCESS;
}

/* exit LT and wait them. */
DECL_EV_HANDLER(do_exit) {
    UNUSED_PARAM(pgm);
    node->exit_mastt = true;
    TM_LOG2("exit", "...", NULL);
    for (t_pgm *pgm_cp = node->head; pgm_cp; pgm_cp = pgm_cp->privy.next)
        if (exit_pgm_launchers(pgm_cp)) return EXIT_FAILURE;
    for (t_pgm *pgm_cp = node->head; pgm_cp; pgm_cp = pgm_cp->privy.next)
        if (join_pgm_launchers(pgm_cp)) return EXIT_FAILURE;
    return EXIT_SUCCESS;
}

/*=================================== init ===================================*/

/* Creates a pool of launcher thread for all programs */
static uint8_t create_thread_pool(t_tm_node *node) {
    t_pgm *pgm = node->head;

    for (uint32_t i = 0; i < node->pgm_nb && pgm; i++) {
        if (create_launcher_pool(pgm)) return EXIT_FAILURE;
        pgm = pgm->privy.next;
    }
    return EXIT_SUCCESS;
}

static uint8_t set_autostart(t_tm_node *node) {
    t_pgm *pgm = node->head;

    for (uint32_t i = 0; i < node->pgm_nb && pgm; i++) {
        if (PGM_SPEC_GET(bool, usr.autostart)) do_start(pgm, node);
        pgm = pgm->privy.next;
    }
    return EXIT_SUCCESS;
}

/*
 * The master thread listen the client events
 * - start, stop, restart, reload, exit - and handle them.
 *
 * @args:
 *   void *arg  is the address of the t_tm_node which is the node
 *              containing the program_specification linked list & metadata
 **/
static void *master_thread(void *arg) {
    t_tm_node *node = arg;
    t_event client_ev;
    uint8_t (*execute_event[CLIENT_MAX_EVENT])(t_pgm *, t_tm_node *) = {
        do_status, do_start, do_restart, do_stop, do_exit, do_add, do_del,
    };

    TM_LOG2("taskmaster", "program started", NULL);
    if (create_thread_pool(node)) return NULL;
    if (set_autostart(node)) return NULL;

    while (node->exit_mastt == false) {
        sem_wait(&node->new_event);
        pthread_mutex_lock(&node->mtx_queue);
        client_ev = node->event_queue[0];
        for (uint32_t i = 0; i < node->ev_queue_sz; i++) {
            node->event_queue[i] = node->event_queue[i + 1];
        }
        node->ev_queue_sz--;
        pthread_mutex_unlock(&node->mtx_queue);
        sem_post(&node->free_place);
        execute_event[client_ev.type](client_ev.pgm, node);
    }
    TM_LOG2("taskmaster", "program exit", NULL);
    return NULL;
}

uint8_t run_server(t_tm_node *node) {
    if (pthread_create(&node->master_thrd, NULL, master_thread, node)) {
        destroy_taskmaster(node);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
