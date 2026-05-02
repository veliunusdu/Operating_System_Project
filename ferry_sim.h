#ifndef FERRY_SIM_H
#define FERRY_SIM_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include <time.h>
#include <stdarg.h>
#include <stdbool.h>
#include <sys/time.h>
#include <errno.h>

/* ─── Simulation parameters ─────────────────────────────────── */
#define NUM_CARS         12
#define NUM_MINIBUSES    10
#define NUM_TRUCKS        8
#define TOTAL_VEHICLES   30   /* 12 + 10 + 8                    */

#define FERRY_MAX_LOAD   20   /* units                          */
#define TOLL_PER_SIDE     2   /* booths per side                */

#define SIDE_A 0
#define SIDE_B 1

/* Departure timeout (ms) – starvation prevention */
#define FERRY_DEPART_TIMEOUT_MS 4000

/* Random delay ranges (ms) */
#define TOLL_DELAY_MIN   100
#define TOLL_DELAY_MAX   500
#define TRAVEL_MIN      1500
#define TRAVEL_MAX      3000
#define RETURN_WAIT_MIN  800
#define RETURN_WAIT_MAX 2500
#define INIT_DELAY_MIN    50
#define INIT_DELAY_MAX   600

/* Ferry states */
typedef enum { FERRY_UNLOADING, FERRY_LOADING, FERRY_TRAVELING } FerryState;

/* ─── Vehicle ────────────────────────────────────────────────── */
typedef struct {
    int         id;
    const char *type_name;
    int         capacity;      /* 1=Car 2=Minibus 3=Truck       */
    int         origin_side;
    int         current_side;  /* -1 = on ferry                 */

    /* Statistics */
    long        queue_enter_ms;
    long        total_wait_ms;
} Vehicle;

/* ─── FIFO Queue (array-based, fixed size) ───────────────────── */
typedef struct {
    int data[TOTAL_VEHICLES];
    int head, tail, size;
} FIFOQueue;

/* ─── Shared globals (defined in ferry_sim.c) ────────────────── */
extern Vehicle     vehicles[TOTAL_VEHICLES];
extern FIFOQueue   queue[2];

/* Ferry state */
extern int        ferry_side;
extern FerryState ferry_state;
extern int        ferry_load;
extern int        ferry_passengers[TOTAL_VEHICLES];
extern int        ferry_passenger_count;
extern int        ferry_trip_count;

/* Loading handshake */
extern int        loading_turn_id;   /* vehicle ID allowed to board, -1 = none */
extern bool       vehicle_boarded;

/* Unloading handshake */
extern int        unloaded_count;    /* how many unloaded this trip */
extern int        to_unload;         /* snapshot of passenger_count at arrival */

/* Completion */
extern int        completed_count;
extern long       sim_start_ms;

/* Synchronisation */
extern pthread_mutex_t ferry_mtx;
extern pthread_cond_t  cond_vehicle_boarded;   /* vehicle → ferry: I boarded */
extern pthread_cond_t  cond_ferry_loading;      /* ferry → vehicles: your turn */
extern pthread_cond_t  cond_ferry_arrived;      /* ferry → passengers: unload now */
extern pthread_cond_t  cond_all_unloaded;       /* passengers → ferry: done */

extern pthread_mutex_t queue_mtx[2];
extern pthread_cond_t  cond_queue[2];           /* vehicles wait here for turn */

extern sem_t           toll_sem[2];

extern pthread_mutex_t log_mtx;
extern pthread_mutex_t stats_mtx;

/* ─── Statistics collected at end ───────────────────────────── */
extern long max_wait_ms;
extern long sum_wait_ms;

/* ─── Function prototypes ────────────────────────────────────── */
long    now_ms(void);
long    elapsed_ms(void);
void    sim_log(const char *fmt, ...);
void    sleep_ms(int ms);
int     rand_range(int lo, int hi);

void    q_init(FIFOQueue *q);
bool    q_empty(FIFOQueue *q);
void    q_enqueue(FIFOQueue *q, int id);
int     q_peek(FIFOQueue *q);
int     q_dequeue(FIFOQueue *q);

void   *ferry_thread_fn(void *arg);
void   *vehicle_thread_fn(void *arg);

#endif /* FERRY_SIM_H */
