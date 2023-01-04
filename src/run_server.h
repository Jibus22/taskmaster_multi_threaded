#ifndef RUN_SERVER_H
#define RUN_SERVER_H

#include "taskmaster.h"

/* usleep() value for master_thread listening loop - in us */
#define START_SUPERVISOR_RATE (4000)
#define STOP_SUPERVISOR_RATE (4000)
#define KILL_TIME_LIMIT (5) /* in sec */

typedef struct timeval tm_timeval_t;
typedef int32_t *int32_Ptr;
typedef uint8_t *uint8_Ptr;
typedef char *char_Ptr;
typedef char **char_PtrPtr;

/* runtime data relative to a thread. One launcher thread has one timer */
typedef struct thread_data {
    /* pthread_mutex_t rw_thrd; */
    pthread_rwlock_t rw_thrd;
    /* constant synchronization between timer & launcher */
    pthread_barrier_t sync_barrier;

    t_tm_node *node; /* pointer to the node */
    t_pgm *pgm;      /* pointer to the related pgm data */

    uint32_t rid;  /* rank id of current thread/proc. Index for an array */
    pthread_t tid; /* thread id of current thread */
    pid_t pid;     /* pid of current process */
    int32_t restart_counter; /* how many time the process can be restarted */
    tm_timeval_t start_timestamp; /* time when process started */

    pthread_mutex_t mtx_wakeup;
    pthread_cond_t
        cond_wakeup; /* conditon variable to signal thread to start */

    /* This variable must be set with its macros
     * bits are ordered as following: eeeessss
     * states: stopped - started - stopping - starting.
     * events: no_event - stop - restart - exit. */
    atomic_uchar info;

    /*    timer    */

    /* semaphore to synchronize timer & launcher thread at init */
    sem_t sync;
    pthread_mutex_t mtx_timer;
    pthread_cond_t cond_timer; /* conditon variable to unlock timer */
    pthread_t timer_id;        /* thread id of start_timer thread */
} t_thread_data;

/* ----- PROCESSUS STATES ----- */

#define PROC_ST_STOPPED (0x00) /* 0000 */
#define PROC_ST_STARTED (0x01) /* 0001 */
/* When stopping, proc is also started - 0011 */
#define PROC_ST_STOPPING (0x03)
/* When starting, proc is also stopped - 0100 */
#define PROC_ST_STARTING (0x04)

/* ----- THREAD EVENTS ----- */

#define THRD_EV_NOEVENT (0x0) /* default. */
#define THRD_EV_STOP (0x1)    /* thread gets idle */
#define THRD_EV_RESTART (0x2) /* thread gets active */
#define THRD_EV_EXIT (0x3)    /* thread gets exited */

/* ----- MASKS ----- */

/* proc is active if is either started, starting or stopping - 0111 */
#define PROC_ACTIVE (0x07)
#define IS_PROC_ACTIVE(ptr) (((ptr)->info & PROC_ACTIVE) > 0)

/* ----- INFO SETTERS & GETTERS ----- */

/*
 * if proc state value to set is starting, it overides restarting event to 0,
 * otherwise not
 **/
#define SET_PROC_STATE(value)                                          \
    do {                                                               \
        thrd->info = (((thrd->info & 0xf0) + ((value)&0x0f)) *         \
                      ((value) != PROC_ST_STARTING)) +                 \
                     (((value)&0x0f) * ((value) == PROC_ST_STARTING)); \
    } while (0)
#define GET_PROC_STATE (thrd->info & 0x0f)

/* set event to info without overriding states */
#define SET_THRD_EVENT(value)                              \
    do {                                                   \
        thrd->info = ((value) << 4) + (thrd->info & 0x0f); \
    } while (0)

#define GET_THRD_EVENT (thrd->info >> 4)

/* ----- EXIT & DEBUG MACROS ----- */

#ifdef DEVELOPEMENT
#define debug_thrd()                                                        \
    do {                                                                    \
        printf("[%-14s- %-2d] - tid %lu - pid %d - cnt %d\n",               \
               PGM_SPEC_GET_T(uint8_Ptr, str_name),                         \
               THRD_DATA_GET(uint32_t, rid), THRD_DATA_GET(pthread_t, tid), \
               THRD_DATA_GET(pid_t, pid),                                   \
               THRD_DATA_GET(int32_t, restart_counter));                    \
        fflush(stdout);                                                     \
    } while (0)
#else
#define debug_thrd()
#endif

#define exit_thrd(thrd, msg, file, func, line) \
    do {                                       \
        exit_thread(thrd);                     \
        err_display(msg, file, func, line);    \
        return (NULL);                         \
    } while (0)

/* ----- THREAD GETTERS & SETTERS ----- */

/*
 * update data in struct thread_data.
 * @args:
 *   name    is the name of the variable from the struct that we want to update
 *   value   is the value we want to give to this variable
 **/
#define THRD_DATA_SET(name, value)             \
    do {                                       \
        pthread_rwlock_wrlock(&thrd->rw_thrd); \
        thrd->name = value;                    \
        pthread_rwlock_unlock(&thrd->rw_thrd); \
    } while (0)

/* generic getters functions to have lock-free value from struct thread_data */
#define THRD_DATA_GET_FUNC(type) thrd_data_get_##type
#define THRD_DATA_GET_CALL(type, value) THRD_DATA_GET_FUNC(type)(thrd, value)

#define THRD_DATA_GET_DECL(type) \
    static type THRD_DATA_GET_FUNC(type)(t_thread_data * thrd, type * value)
#define THRD_DATA_GET_IMPLEMENTATION(type)     \
    THRD_DATA_GET_DECL(type) {                 \
        type save;                             \
                                               \
        pthread_rwlock_rdlock(&thrd->rw_thrd); \
        save = *value;                         \
        pthread_rwlock_unlock(&thrd->rw_thrd); \
        return save;                           \
    }

/* thread_data getter. name is the name of the struct variable name */
#define THRD_DATA_GET(type, name) THRD_DATA_GET_CALL(type, &thrd->name)

/* ----- PGM GETTERS & SETTERS ----- */

/*
 * update data in struct program_specification.
 * @args:
 *   name    is the name of the variable from the struct that we want to update
 *   value   is the value we want to give to this variable
 **/
#define PGM_SPEC_SET_T(name, value)                \
    do {                                           \
        pthread_rwlock_wrlock(&thrd->pgm->rw_pgm); \
        thrd->pgm->name = value;                   \
        pthread_rwlock_unlock(&thrd->pgm->rw_pgm); \
    } while (0)
#define PGM_SPEC_SET(name, value)                  \
    do {                                           \
        pthread_rwlock_wrlock(&pgm->privy.rw_pgm); \
        pgm->name = value;                         \
        pthread_rwlock_unlock(&pgm->privy.rw_pgm); \
    } while (0)

/* generic getters functions to have lock-free value from struct
 * program_specification */
#define PGM_SPEC_GET_FUNC(type) pgm_spec_get_##type
#define PGM_SPEC_GET_CALL(type, obj, name) \
    PGM_SPEC_GET_FUNC(type)(obj, &(obj->name))

#define PGM_SPEC_GET_DECL(type) \
    static type PGM_SPEC_GET_FUNC(type)(t_pgm * pgm, type * value)
#define PGM_SPEC_GET_IMPLEMENTATION(type)          \
    PGM_SPEC_GET_DECL(type) {                      \
        type save;                                 \
                                                   \
        pthread_rwlock_rdlock(&pgm->privy.rw_pgm); \
        save = *value;                             \
        pthread_rwlock_unlock(&pgm->privy.rw_pgm); \
        return save;                               \
    }

/* pgm_spec getter. name is the name of the struct variable name */
#define PGM_SPEC_GET_T(type, name) \
    PGM_SPEC_GET_CALL(type, (thrd->pgm), name) /* get from launcher thread */

#define PGM_SPEC_GET(type, name) \
    PGM_SPEC_GET_CALL(type, (pgm), name) /* get from master thread */

/* ----- PGM STATE GETTERS & SETTERS ----- */

/*
 * update program_state data into struct program_specification.
 * @args:
 *   name    is the name of the variable from the struct that we want to update
 *   value   is the value we want to give to this variable
 **/
#define PGM_STATE_SET_T(name, value)                     \
    do {                                                 \
        pthread_mutex_lock(&thrd->pgm->mtx_pgm_state);   \
        thrd->pgm->program_state.name = value;           \
        pthread_mutex_unlock(&thrd->pgm->mtx_pgm_state); \
    } while (0)
#define PGM_STATE_SET(name, value)                 \
    do {                                           \
        pthread_mutex_lock(&pgm->mtx_pgm_state);   \
        pgm->program_state.name = value;           \
        pthread_mutex_unlock(&pgm->mtx_pgm_state); \
    } while (0)

/*
 * program_state getter (thru a program_specification struct).
 * name is the name of the struct variable name
 **/
/* #define PGM_STATE_GET_T(name) thrd->pgm->program_state.name */
/* #define PGM_STATE_GET(name) pgm->program_state.name */
#define PGM_STATE_GET_T(name) pgm_state_getter_t(thrd, name)
#define PGM_STATE_GET(name) pgm_state_getter(pgm, name)

/* ----- LOGGING MACROS ----- */

#define BUF_LOG_LEN 256
#define TM_LOG(func, fmt, ...)                                                \
    do {                                                                      \
        if (pthread_mutex_lock(&thrd->node->mtx_log)) break;                  \
        char buf[BUF_LOG_LEN] = {0};                                          \
        time_t curtime = time(NULL);                                          \
        struct tm loctime;                                                    \
        size_t len;                                                           \
        if (localtime_r(&curtime, &loctime) != &loctime) break;               \
                                                                              \
        len = strftime(buf, BUF_LOG_LEN, "%F, %T ", &loctime);                \
        len += snprintf(buf + len, BUF_LOG_LEN - len, "- [%17s] - " fmt "\n", \
                        func, __VA_ARGS__);                                   \
        fwrite(buf, sizeof(char), len, thrd->node->tm_stream_log);            \
        fflush(thrd->node->tm_stream_log);                                    \
        if (pthread_mutex_unlock(&thrd->node->mtx_log)) break;                \
    } while (0)
#define TM_LOG2(func, fmt, ...)                                   \
    do {                                                          \
        if (pthread_mutex_lock(&node->mtx_log)) break;            \
        char buf[BUF_LOG_LEN] = {0};                              \
        time_t curtime = time(NULL);                              \
        struct tm loctime;                                        \
        size_t len;                                               \
        if (localtime_r(&curtime, &loctime) != &loctime) break;   \
                                                                  \
        len = strftime(buf, BUF_LOG_LEN, "%F, %T ", &loctime);    \
        len += snprintf(buf + len, BUF_LOG_LEN - len,             \
                        "- [" func "] - " fmt "\n", __VA_ARGS__); \
        fwrite(buf, sizeof(char), len, node->tm_stream_log);      \
        fflush(node->tm_stream_log);                              \
        if (pthread_mutex_unlock(&node->mtx_log)) break;          \
    } while (0)

#define TM_THRD_LOG(status)                                                \
    TM_LOG("launcher thread",                                              \
           "[%s pid[%d]] - tid[%lu] - restart_counter[%d] • [" status "]", \
           PGM_SPEC_GET_T(char_Ptr, usr.name), THRD_DATA_GET(pid_t, pid),  \
           THRD_DATA_GET(pthread_t, tid),                                  \
           THRD_DATA_GET(int32_t, restart_counter));

#define TM_CHILDCONTROL_LOG(status)                                           \
    TM_LOG("child supervisor",                                                \
           "[%s pid[%d]] - tid[%lu] - restart_counter[%d] • [" status " %d]", \
           PGM_SPEC_GET_T(char_Ptr, usr.name), THRD_DATA_GET(pid_t, pid),     \
           THRD_DATA_GET(pthread_t, tid),                                     \
           THRD_DATA_GET(int32_t, restart_counter), child_ret);

#define TM_STOP_LOG(status)                                                   \
    TM_LOG("stop timer",                                                      \
           "[%s] - tid[%lu] - rank[%d] - stop_time[%d ms] • [" status "]",    \
           PGM_SPEC_GET_T(char_Ptr, usr.name), THRD_DATA_GET(pthread_t, tid), \
           THRD_DATA_GET(uint32_t, rid),                                      \
           PGM_SPEC_GET_T(uint32_t, usr.stoptime));

#define TM_START_LOG(status)                                                   \
    TM_LOG("start timer",                                                      \
           "[%s pid[%d]] - tid[%lu] - rank[%d] - start_time[%d ms] • [" status \
           "]",                                                                \
           PGM_SPEC_GET_T(char_Ptr, usr.name), pid,                            \
           THRD_DATA_GET(pthread_t, tid), THRD_DATA_GET(uint32_t, rid),        \
           PGM_SPEC_GET_T(uint32_t, usr.starttime));

#endif
