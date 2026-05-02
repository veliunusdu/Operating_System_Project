/*
 * ferry_sim.c — Ferry Transport Simulation
 * Multithreaded system using POSIX threads, semaphores, mutexes,
 * and condition variables.
 *
 * Compile:  gcc -o ferry_sim ferry_sim.c -lpthread -lrt
 * Run:      ./ferry_sim
 */

#include "ferry_sim.h"

/* ================================================================
 *  GLOBAL DEFINITIONS
 * ================================================================ */

Vehicle    vehicles[TOTAL_VEHICLES];
FIFOQueue  queue[2];

int        ferry_side;
FerryState ferry_state;
int        ferry_load;
int        ferry_passengers[TOTAL_VEHICLES];
int        ferry_passenger_count;
int        ferry_trip_count;

int        loading_turn_id = -1;
bool       vehicle_boarded = false;

int        unloaded_count = 0;
int        to_unload      = 0;

int        completed_count = 0;
long       sim_start_ms;

long       max_wait_ms = 0;
long       sum_wait_ms = 0;

/* Mutexes & condition variables */
pthread_mutex_t ferry_mtx            = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  cond_vehicle_boarded = PTHREAD_COND_INITIALIZER;
pthread_cond_t  cond_ferry_loading   = PTHREAD_COND_INITIALIZER;
pthread_cond_t  cond_ferry_arrived   = PTHREAD_COND_INITIALIZER;
pthread_cond_t  cond_all_unloaded    = PTHREAD_COND_INITIALIZER;

pthread_mutex_t queue_mtx[2] = { PTHREAD_MUTEX_INITIALIZER,
                                  PTHREAD_MUTEX_INITIALIZER };
pthread_cond_t  cond_queue[2] = { PTHREAD_COND_INITIALIZER,
                                   PTHREAD_COND_INITIALIZER };

sem_t           toll_sem[2];

pthread_mutex_t log_mtx   = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t stats_mtx = PTHREAD_MUTEX_INITIALIZER;

/* ================================================================
 *  UTILITY
 * ================================================================ */

long now_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000L + tv.tv_usec / 1000L;
}

long elapsed_ms(void) { return now_ms() - sim_start_ms; }

void sim_log(const char *fmt, ...) {
    pthread_mutex_lock(&log_mtx);
    printf("[%6ld ms] ", elapsed_ms());
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    putchar('\n');
    fflush(stdout);
    pthread_mutex_unlock(&log_mtx);
}

void sleep_ms(int ms) {
    struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

int rand_range(int lo, int hi) {
    return lo + rand() % (hi - lo + 1);
}

static struct timespec deadline_from_now(int ms) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec  += ms / 1000;
    ts.tv_nsec += (long)(ms % 1000) * 1000000L;
    if (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
    return ts;
}

static const char *side_str(int s) { return s == SIDE_A ? "Side-A" : "Side-B"; }

/* ================================================================
 *  FIFO QUEUE
 * ================================================================ */

void q_init(FIFOQueue *q)              { q->head = q->tail = q->size = 0; }
bool q_empty(FIFOQueue *q)             { return q->size == 0; }
int  q_peek(FIFOQueue *q)              { return q->size ? q->data[q->head] : -1; }

void q_enqueue(FIFOQueue *q, int id) {
    q->data[q->tail] = id;
    q->tail = (q->tail + 1) % TOTAL_VEHICLES;
    q->size++;
}

int q_dequeue(FIFOQueue *q) {
    if (!q->size) return -1;
    int id = q->data[q->head];
    q->head = (q->head + 1) % TOTAL_VEHICLES;
    q->size--;
    return id;
}

/* ================================================================
 *  FERRY THREAD
 * ================================================================ */

void *ferry_thread_fn(void *arg) {
    (void)arg;
    sim_log("Ferry CREATED — starting on %s", side_str(ferry_side));

    for (;;) {
        /* ── Check termination ────────────────────────── */
        pthread_mutex_lock(&stats_mtx);
        bool done = (completed_count == TOTAL_VEHICLES);
        pthread_mutex_unlock(&stats_mtx);
        if (done) break;

        /* ════ UNLOADING PHASE ════════════════════════ */
        pthread_mutex_lock(&ferry_mtx);
        ferry_state = FERRY_UNLOADING;
        to_unload   = ferry_passenger_count;
        unloaded_count = 0;

        if (to_unload > 0) {
            sim_log("Ferry ARRIVED at %s (load=%d units, %d vehicles) — unloading",
                    side_str(ferry_side), ferry_load, to_unload);
            /* Wake all passengers */
            pthread_cond_broadcast(&cond_ferry_arrived);
            /* Wait until every passenger has unloaded */
            while (unloaded_count < to_unload)
                pthread_cond_wait(&cond_all_unloaded, &ferry_mtx);
            sim_log("Ferry unloading COMPLETE at %s", side_str(ferry_side));
        } else {
            sim_log("Ferry ARRIVED at %s (empty) — ready to load",
                    side_str(ferry_side));
        }
        /* Reset for new trip */
        ferry_load            = 0;
        ferry_passenger_count = 0;

        /* ════ LOADING PHASE ══════════════════════════ */
        ferry_state     = FERRY_LOADING;
        loading_turn_id = -1;
        vehicle_boarded = false;
        pthread_mutex_unlock(&ferry_mtx);

        /* Wake vehicles waiting in queue on this side */
        pthread_mutex_lock(&queue_mtx[ferry_side]);
        pthread_cond_broadcast(&cond_queue[ferry_side]);
        pthread_mutex_unlock(&queue_mtx[ferry_side]);

        /* Load vehicles one at a time under ferry_mtx */
        pthread_mutex_lock(&ferry_mtx);
        struct timespec dl = deadline_from_now(FERRY_DEPART_TIMEOUT_MS);

        for (;;) {
            /* Peek at queue head */
            pthread_mutex_lock(&queue_mtx[ferry_side]);
            bool empty   = q_empty(&queue[ferry_side]);
            int  head_id = q_peek(&queue[ferry_side]);
            pthread_mutex_unlock(&queue_mtx[ferry_side]);

            int head_cap = (head_id >= 0) ? vehicles[head_id].capacity : 0;

            /* Departure condition 1: full */
            if (ferry_load == FERRY_MAX_LOAD) {
                sim_log("Ferry FULL (%d units). Departing %s",
                        ferry_load, side_str(ferry_side));
                break;
            }
            /* Departure condition 2: head vehicle doesn't fit */
            if (!empty && ferry_load + head_cap > FERRY_MAX_LOAD) {
                sim_log("Head vehicle (%d units) does not fit (remaining=%d). "
                        "Departing %s",
                        head_cap, FERRY_MAX_LOAD - ferry_load, side_str(ferry_side));
                break;
            }
            /* Departure condition 3: timeout */
            if (empty) {
                /* Timed wait for a vehicle to join queue */
                int rc = pthread_cond_timedwait(&cond_vehicle_boarded,
                                                &ferry_mtx, &dl);
                /* Re-check after wake */
                if (rc == ETIMEDOUT) {
                    sim_log("Ferry timeout — departing %s with load=%d units",
                            side_str(ferry_side), ferry_load);
                    break;
                }
                continue;
            }

            /* Grant boarding token to head vehicle */
            loading_turn_id = head_id;
            vehicle_boarded = false;

            /* Signal queued vehicles */
            pthread_mutex_lock(&queue_mtx[ferry_side]);
            pthread_cond_broadcast(&cond_queue[ferry_side]);
            pthread_mutex_unlock(&queue_mtx[ferry_side]);

            /* Wait until that vehicle boards */
            while (!vehicle_boarded)
                pthread_cond_wait(&cond_vehicle_boarded, &ferry_mtx);

            loading_turn_id = -1;
            /* Reset timeout after each successful boarding */
            dl = deadline_from_now(FERRY_DEPART_TIMEOUT_MS);
        }

        /* ════ DEPART ═════════════════════════════════ */
        ferry_state = FERRY_TRAVELING;
        ferry_trip_count++;
        int next_side = 1 - ferry_side;

        sim_log("Ferry DEPARTED %s → %s  [trip #%d | load=%d units | %d vehicles]",
                side_str(ferry_side), side_str(next_side),
                ferry_trip_count, ferry_load, ferry_passenger_count);

        /* Tell any vehicles left on departing side that ferry is gone */
        pthread_mutex_lock(&queue_mtx[ferry_side]);
        pthread_cond_broadcast(&cond_queue[ferry_side]);
        pthread_mutex_unlock(&queue_mtx[ferry_side]);

        pthread_mutex_unlock(&ferry_mtx);

        /* ── Travel ───────────────────────────────────── */
        sleep_ms(rand_range(TRAVEL_MIN, TRAVEL_MAX));
        ferry_side = next_side;

        /* Termination re-check after travel */
        pthread_mutex_lock(&stats_mtx);
        done = (completed_count == TOTAL_VEHICLES);
        pthread_mutex_unlock(&stats_mtx);
        if (done) break;
    }

    sim_log("Ferry thread FINISHED. Total trips: %d", ferry_trip_count);
    return NULL;
}

/* ================================================================
 *  VEHICLE THREAD — helper: pass through toll
 * ================================================================ */
static void do_toll(Vehicle *v, int side) {
    sim_log("%s-%d waiting for toll at %s",
            v->type_name, v->id, side_str(side));
    sem_wait(&toll_sem[side]);

    sim_log("%s-%d entered toll booth at %s",
            v->type_name, v->id, side_str(side));
    sleep_ms(rand_range(TOLL_DELAY_MIN, TOLL_DELAY_MAX));
    sim_log("%s-%d exited toll booth at %s",
            v->type_name, v->id, side_str(side));

    sem_post(&toll_sem[side]);
}

/* ================================================================
 *  VEHICLE THREAD — helper: queue up and board ferry
 * ================================================================ */
static void do_board(Vehicle *v, int side) {
    /* ── Join FIFO queue ─────────────────────────────── */
    pthread_mutex_lock(&queue_mtx[side]);
    q_enqueue(&queue[side], v->id);
    v->queue_enter_ms = elapsed_ms();
    sim_log("%s-%d joined queue at %s  (queue size: %d)",
            v->type_name, v->id, side_str(side), queue[side].size);

    /*
     * Wait until:
     *   1. Ferry is LOADING on our side, AND
     *   2. loading_turn_id == our ID
     * We release queue_mtx while sleeping, so the ferry thread
     * can check / update queue state.
     */
    for (;;) {
        pthread_mutex_lock(&ferry_mtx);
        bool our_turn = (ferry_state  == FERRY_LOADING)
                     && (ferry_side   == side)
                     && (loading_turn_id == v->id);
        pthread_mutex_unlock(&ferry_mtx);

        if (our_turn) break;

        pthread_cond_wait(&cond_queue[side], &queue_mtx[side]);
    }

    /* Dequeue ourselves (we are guaranteed to be the head) */
    q_dequeue(&queue[side]);
    long waited = elapsed_ms() - v->queue_enter_ms;
    pthread_mutex_unlock(&queue_mtx[side]);

    /* Update statistics */
    pthread_mutex_lock(&stats_mtx);
    v->total_wait_ms += waited;
    sum_wait_ms      += waited;
    if (waited > max_wait_ms) max_wait_ms = waited;
    pthread_mutex_unlock(&stats_mtx);

    /* ── Board (loading critical section) ─────────────── */
    pthread_mutex_lock(&ferry_mtx);
    ferry_load += v->capacity;
    ferry_passengers[ferry_passenger_count++] = v->id;
    vehicle_boarded  = true;
    v->current_side  = -1; /* on ferry */

    sim_log("%s-%d BOARDED at %s  (ferry load = %d/%d units | waited %ld ms)",
            v->type_name, v->id, side_str(side),
            ferry_load, FERRY_MAX_LOAD, waited);

    pthread_cond_signal(&cond_vehicle_boarded);
    pthread_mutex_unlock(&ferry_mtx);
}

/* ================================================================
 *  VEHICLE THREAD — helper: ride ferry and unload
 * ================================================================ */
static void do_unload(Vehicle *v, int dest) {
    pthread_mutex_lock(&ferry_mtx);
    /* Wait until ferry arrives at dest and enters UNLOADING state */
    while (!(ferry_state == FERRY_UNLOADING && ferry_side == dest))
        pthread_cond_wait(&cond_ferry_arrived, &ferry_mtx);

    unloaded_count++;
    v->current_side = dest;
    sim_log("%s-%d UNLOADED at %s  (%d/%d unloaded)",
            v->type_name, v->id, side_str(dest),
            unloaded_count, to_unload);

    pthread_cond_signal(&cond_all_unloaded);
    pthread_mutex_unlock(&ferry_mtx);
}

/* ================================================================
 *  VEHICLE THREAD
 * ================================================================ */
void *vehicle_thread_fn(void *arg) {
    Vehicle *v = (Vehicle *)arg;
    int origin = v->origin_side;
    int dest   = 1 - origin;

    sim_log("%s-%d CREATED — origin %s, capacity=%d unit(s)",
            v->type_name, v->id, side_str(origin), v->capacity);

    /* Random stagger so not all vehicles hit the toll at once */
    sleep_ms(rand_range(INIT_DELAY_MIN, INIT_DELAY_MAX));

    /* ════ TRIP 1: origin → destination ══════════════ */
    do_toll(v, origin);
    do_board(v, origin);
    do_unload(v, dest);

    /* ── Wait before return trip ──────────────────── */
    int wait_time = rand_range(RETURN_WAIT_MIN, RETURN_WAIT_MAX);
    sim_log("%s-%d waiting %d ms before return trip", v->type_name, v->id, wait_time);
    sleep_ms(wait_time);

    /* ════ TRIP 2: destination → origin ══════════════ */
    do_toll(v, dest);
    do_board(v, dest);
    do_unload(v, origin);

    /* ── Round trip complete ───────────────────────── */
    pthread_mutex_lock(&stats_mtx);
    completed_count++;
    sim_log("%s-%d COMPLETED round trip  (total wait = %ld ms | completed %d/%d)",
            v->type_name, v->id, v->total_wait_ms,
            completed_count, TOTAL_VEHICLES);
    pthread_mutex_unlock(&stats_mtx);

    return NULL;
}

/* ================================================================
 *  INITIALISATION
 * ================================================================ */
static void init_vehicles(void) {
    int idx = 0;
    /* Cars */
    for (int i = 0; i < NUM_CARS; i++, idx++) {
        vehicles[idx].id          = idx;
        vehicles[idx].type_name   = "Car";
        vehicles[idx].capacity    = 1;
        vehicles[idx].origin_side = rand() & 1;
        vehicles[idx].current_side= vehicles[idx].origin_side;
        vehicles[idx].total_wait_ms = 0;
    }
    /* Minibuses */
    for (int i = 0; i < NUM_MINIBUSES; i++, idx++) {
        vehicles[idx].id          = idx;
        vehicles[idx].type_name   = "Minibus";
        vehicles[idx].capacity    = 2;
        vehicles[idx].origin_side = rand() & 1;
        vehicles[idx].current_side= vehicles[idx].origin_side;
        vehicles[idx].total_wait_ms = 0;
    }
    /* Trucks */
    for (int i = 0; i < NUM_TRUCKS; i++, idx++) {
        vehicles[idx].id          = idx;
        vehicles[idx].type_name   = "Truck";
        vehicles[idx].capacity    = 3;
        vehicles[idx].origin_side = rand() & 1;
        vehicles[idx].current_side= vehicles[idx].origin_side;
        vehicles[idx].total_wait_ms = 0;
    }
}

/* ================================================================
 *  STATISTICS REPORT
 * ================================================================ */
static void print_stats(long total_ms) {
    printf("\n");
    printf("╔══════════════════════════════════════════════════╗\n");
    printf("║           SIMULATION STATISTICS REPORT           ║\n");
    printf("╠══════════════════════════════════════════════════╣\n");
    printf("║  Total simulation time   : %8ld ms            ║\n", total_ms);
    printf("║  Total ferry trips       : %8d               ║\n", ferry_trip_count);
    printf("║  Vehicles completed      : %8d / %-3d          ║\n",
           completed_count, TOTAL_VEHICLES);
    printf("║  Total queue wait        : %8ld ms            ║\n", sum_wait_ms);
    printf("║  Average wait per vehicle: %8ld ms            ║\n",
           TOTAL_VEHICLES > 0 ? sum_wait_ms / TOTAL_VEHICLES : 0);
    printf("║  Maximum wait (any veh.) : %8ld ms            ║\n", max_wait_ms);

    /* Ferry utilisation: total load-units carried / (trips * FERRY_MAX_LOAD) */
    /* We need total units carried — compute from vehicles */
    long total_units = 0;
    for (int i = 0; i < TOTAL_VEHICLES; i++)
        total_units += (long)vehicles[i].capacity * 2; /* 2 trips each */
    double util = ferry_trip_count > 0
                ? (double)total_units / ((double)ferry_trip_count * FERRY_MAX_LOAD) * 100.0
                : 0.0;
    printf("║  Ferry utilisation ratio : %7.1f %%             ║\n", util);
    printf("╚══════════════════════════════════════════════════╝\n");

    /* Per-vehicle table */
    printf("\n  %-10s  %5s  %8s  %12s\n",
           "Vehicle", "Cap", "Side", "Wait (ms)");
    printf("  %-10s  %5s  %8s  %12s\n",
           "----------", "---", "--------", "------------");
    for (int i = 0; i < TOTAL_VEHICLES; i++) {
        Vehicle *v = &vehicles[i];
        char name[24];
        snprintf(name, sizeof(name), "%s-%d", v->type_name, v->id);
        printf("  %-10s  %5d  %8s  %12ld\n",
               name, v->capacity,
               side_str(v->origin_side), v->total_wait_ms);
    }
    printf("\n");
}

/* ================================================================
 *  MAIN
 * ================================================================ */
int main(void) {
    srand((unsigned)time(NULL));
    sim_start_ms = now_ms();

    printf("╔══════════════════════════════════════════════════╗\n");
    printf("║       FERRY TRANSPORT SIMULATION — START         ║\n");
    printf("╚══════════════════════════════════════════════════╝\n\n");

    /* Initialise queues */
    q_init(&queue[SIDE_A]);
    q_init(&queue[SIDE_B]);

    /* Initialise toll semaphores: 2 booths per side */
    sem_init(&toll_sem[SIDE_A], 0, TOLL_PER_SIDE);
    sem_init(&toll_sem[SIDE_B], 0, TOLL_PER_SIDE);

    /* Ferry starts on a random side */
    ferry_side            = rand() & 1;
    ferry_state           = FERRY_LOADING;
    ferry_load            = 0;
    ferry_passenger_count = 0;
    ferry_trip_count      = 0;

    /* Initialise vehicles */
    init_vehicles();

    /* ── Launch ferry thread ───────────────────────── */
    pthread_t ferry_tid;
    pthread_create(&ferry_tid, NULL, ferry_thread_fn, NULL);

    /* ── Launch vehicle threads ───────────────────── */
    pthread_t vtids[TOTAL_VEHICLES];
    for (int i = 0; i < TOTAL_VEHICLES; i++)
        pthread_create(&vtids[i], NULL, vehicle_thread_fn, &vehicles[i]);

    /* ── Join all vehicle threads ─────────────────── */
    for (int i = 0; i < TOTAL_VEHICLES; i++)
        pthread_join(vtids[i], NULL);

    /* ── Signal ferry to exit (all done) ─────────── */
    pthread_join(ferry_tid, NULL);

    long total_ms = elapsed_ms();

    printf("\n╔══════════════════════════════════════════════════╗\n");
    printf("║        SIMULATION COMPLETE                       ║\n");
    printf("╚══════════════════════════════════════════════════╝\n");

    print_stats(total_ms);

    /* Cleanup */
    sem_destroy(&toll_sem[SIDE_A]);
    sem_destroy(&toll_sem[SIDE_B]);

    return 0;
}
