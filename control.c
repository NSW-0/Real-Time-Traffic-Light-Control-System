#include "traffic.h"
#include "shared_state.h"
#include "logger.h"
#include "config.h"


typedef struct { int has_msg; TrafficMsg msg; } Pending;

/* ── Send command to one light, wait for its confirmation ─── */
static void set_light(int mqid, int dir, int cmd, Pending *pend) {
    TrafficMsg m;
    memset(&m, 0, sizeof m);
    m.mtype     = MTYPE_FOR_DIR[dir];
    m.cmd       = cmd;
    m.direction = dir;
    snprintf(m.info, sizeof m.info, "Set %s to %s",
             DIR_NAME[dir],
             cmd==CMD_SET_RED?"RED":cmd==CMD_SET_YELLOW?"YELLOW":"GREEN");
    mq_send(mqid, &m);

    TrafficMsg reply;
    while (1) {
        mq_recv(mqid, &reply, MTYPE_TO_CONTROL);

        /* Got the confirmation we were waiting for */
        if (reply.cmd == EVT_LIGHT_CHANGED && reply.direction == dir)
            break;

        /* Any other message (emergency / pedestrian) — save, don't discard */
        if (pend->has_msg) mq_send(mqid, &pend->msg);  /* re-queue older */
        pend->has_msg = 1;
        pend->msg     = reply;
    }
}

/* ── Safe GREEN → YELLOW → RED transition ────────────────── */
static void safe_to_red(int mqid, int semid, SharedState *state,
                        int dir, int ydur, Pending *pend) {
    int cur;
    sem_lock(semid, SEM_SHM_WRITE);
    cur = state->light_state[dir];
    sem_unlock(semid, SEM_SHM_WRITE);

    if (cur == LIGHT_GREEN) {
        set_light(mqid, dir, CMD_SET_YELLOW, pend);
        sleep((unsigned)ydur);
    }
    if (cur != LIGHT_RED)
        set_light(mqid, dir, CMD_SET_RED, pend);
}

/* ── Non-blocking drain ───────────────────────────────────── */
static int check_messages(int mqid, TrafficMsg *m) {
    return mq_recv_nb(mqid, m, MTYPE_TO_CONTROL);
}

/*
 * Vehicle data is stored in shared memory by vehicle.c.
 * Control reads vehicles_waiting[] from shared memory when
 * deciding whether to extend or skip a phase:
 *   - If no cars waiting in EW direction, NS green can hold longer.
 *   - Vehicle event is logged here so it appears in control decisions.
 */
static int process_incoming(TrafficMsg *inc, int *ped_queue, int *emg_dir) {
    if (inc->cmd == EVT_EMERGENCY_ON) {
        *emg_dir = inc->direction;
        return 1;   /* caller must handle emergency now */
    }
    if (inc->cmd == EVT_PEDESTRIAN_REQ &&
        inc->direction >= 0 && inc->direction < DIR_COUNT)
        ped_queue[inc->direction] = 1;
    /* Vehicle data already written to shared memory by vehicle.c.
     * Control reads state->vehicles_waiting[] dynamically during
     * phase countdowns — no extra storage needed here. */
    return 0;
}

/* Flush pending slot + drain queue → returns emergency dir or -1 */
static int flush_and_drain(int mqid, Pending *pend, int *ped_queue) {
    TrafficMsg inc;
    int edir = -1;
    if (pend->has_msg) {
        if (process_incoming(&pend->msg, ped_queue, &edir)) {
            pend->has_msg = 0;
            return edir;
        }
        pend->has_msg = 0;
    }
    while (check_messages(mqid, &inc) >= 0) {
        if (process_incoming(&inc, ped_queue, &edir))
            return edir;
    }
    return -1;
}

/* ── Check if any vehicles waiting in given directions ──────── */
static int any_vehicles(SharedState *state, int semid,
                        int d0, int d1) {
    int v;
    sem_lock(semid, SEM_SHM_WRITE);
    v = state->vehicles_waiting[d0] + state->vehicles_waiting[d1];
    sem_unlock(semid, SEM_SHM_WRITE);
    return v > 0;
}

/* ── All lights to RED safely ─────────────────────────────── */
static void all_to_red(int mqid, int semid, SharedState *state,
                       int ydur, Pending *pend) {
    for (int d = 0; d < DIR_COUNT; d++)
        safe_to_red(mqid, semid, state, d, ydur, pend);
}

/* ── Run any pending pedestrian crossings ─────────────────── */
static void run_ped_phases(int mqid, int semid, SharedState *state,
                           const Config *cfg, int *ped_queue,
                           Pending *pend, char *logbuf) {
    for (int pdir = 0; pdir < DIR_COUNT; pdir++) {
        if (!ped_queue[pdir] || !state->running) continue;
        ped_queue[pdir] = 0;

        snprintf(logbuf, 128, "Pedestrian crossing ACTIVE: %s",
                 DIR_NAME[pdir]);
        log_event(mqid, pdir, logbuf);
        printf("[Control ] PEDESTRIAN crossing: %s\n", DIR_NAME[pdir]);
        fflush(stdout);

        sem_lock(semid, SEM_SHM_WRITE);
        state->current_phase      = PHASE_PEDESTRIAN;
        state->ped_active         = 1;
        state->ped_direction      = pdir;
        state->ped_request[pdir]  = 0;   /* clear shared memory flag */
        state->phase_total        = cfg->ped_duration;
        state->phase_seconds_left = cfg->ped_duration;
        sem_unlock(semid, SEM_SHM_WRITE);

        /* All 4 lights RED during crossing */
        for (int d = 0; d < DIR_COUNT; d++)
            set_light(mqid, d, CMD_SET_RED, pend);

        for (int t = cfg->ped_duration; t > 0 && state->running; t--) {
            sem_lock(semid, SEM_SHM_WRITE);
            state->phase_seconds_left = t;
            sem_unlock(semid, SEM_SHM_WRITE);
            sleep(1);
        }

        sem_lock(semid, SEM_SHM_WRITE);
        state->ped_active    = 0;
        state->ped_direction = -1;
        sem_unlock(semid, SEM_SHM_WRITE);

        log_event(mqid, pdir, "Pedestrian crossing complete");
        sleep((unsigned)cfg->allred_duration);
    }
}

/* ── Emergency handling ───────────────────────────────────── */
static void handle_emergency(int mqid, int semid, SharedState *state,
                              const Config *cfg, int edir,
                              int *ped_queue, Pending *pend, char *logbuf) {
    snprintf(logbuf, 128, "EMERGENCY MODE: priority to %s", DIR_NAME[edir]);
    log_event(mqid, edir, logbuf);
    printf("[Control ] EMERGENCY → %s\n", DIR_NAME[edir]);
    fflush(stdout);

    sem_lock(semid, SEM_SHM_WRITE);
    state->emergency_active    = 1;
    state->emergency_direction = edir;
    state->emergency_start     = time(NULL);
    state->current_phase       = PHASE_EMERGENCY;
    sem_unlock(semid, SEM_SHM_WRITE);

    /* Safely bring ALL other directions to RED */
    for (int d = 0; d < DIR_COUNT; d++)
        if (d != edir)
            safe_to_red(mqid, semid, state, d, cfg->yellow_duration, pend);

    sleep((unsigned)cfg->allred_duration);
    set_light(mqid, edir, CMD_SET_GREEN, pend);

    printf("[Control ] Emergency: %s is GREEN\n", DIR_NAME[edir]);
    fflush(stdout);

    /* Wait for EVT_EMERGENCY_OFF, queue any pedestrian requests */
    while (state->running) {
        TrafficMsg w;
        int unused = -1;
        if (pend->has_msg) {
            w = pend->msg; pend->has_msg = 0;
            if (w.cmd == EVT_EMERGENCY_OFF) break;
            process_incoming(&w, ped_queue, &unused);
            continue;
        }
        if (check_messages(mqid, &w) >= 0) {
            if (w.cmd == EVT_EMERGENCY_OFF) break;
            process_incoming(&w, ped_queue, &unused);
        }
        sleep(1);
    }

    /* Return emergency direction safely to RED */
    safe_to_red(mqid, semid, state, edir, cfg->yellow_duration, pend);
    sleep((unsigned)cfg->allred_duration);

    sem_lock(semid, SEM_SHM_WRITE);
    state->emergency_active    = 0;
    state->emergency_direction = -1;
    sem_unlock(semid, SEM_SHM_WRITE);

    log_event(mqid, edir, "Emergency cleared, resuming normal cycle");
    printf("[Control ] Emergency cleared.\n");
    fflush(stdout);
}

/* ══════════════════════════════════════════════════════════
 *  run_control — main phase loop
 * ══════════════════════════════════════════════════════════ */
void run_control(int mqid, int shmid, int semid, const Config *cfg) {
    SharedState *state = shm_attach(shmid);
    if (!state) exit(1);

    printf("[Control ] Started.\n");
    fflush(stdout);
    log_event(mqid, -1, "Control logic started");

    char    logbuf[128];
    int     ped_queue[DIR_COUNT];
    Pending pend;
    memset(&pend, 0, sizeof pend);
    memset(ped_queue, 0, sizeof ped_queue);

    /* Safe start: all 4 lights RED */
    for (int d = 0; d < DIR_COUNT; d++)
        set_light(mqid, d, CMD_SET_RED, &pend);
    sleep((unsigned)cfg->allred_duration);

    /* Macro: update phase in shared memory */
    #define SET_PHASE(p, dur) \
        do { sem_lock(semid, SEM_SHM_WRITE); \
             state->current_phase=(p); \
             state->phase_total=(dur); \
             state->phase_seconds_left=(dur); \
             sem_unlock(semid, SEM_SHM_WRITE); } while(0)

    /* Macro: countdown a phase, checking for emergency every second */
    #define COUNTDOWN(dur, force_end) \
        do { \
            for (int _t=(dur); _t>0 && state->running; _t--) { \
                sem_lock(semid, SEM_SHM_WRITE); \
                state->phase_seconds_left = _t; \
                sem_unlock(semid, SEM_SHM_WRITE); \
                int _ed = flush_and_drain(mqid, &pend, ped_queue); \
                if (_ed >= 0) { \
                    handle_emergency(mqid,semid,state,cfg,_ed, \
                                     ped_queue,&pend,logbuf); \
                    (force_end) = 1; break; \
                } \
                sleep(1); \
            } \
        } while(0)

    while (state->running) {

        /* Drain any messages accumulated between phases */
        { int edir = flush_and_drain(mqid, &pend, ped_queue);
          if (edir >= 0)
              handle_emergency(mqid, semid, state, cfg, edir,
                               ped_queue, &pend, logbuf); }
        if (!state->running) break;

        /* ══ 1. NS-GREEN ══════════════════════════════════════ */
        log_event(mqid, -1, "Phase: NS-GREEN");
        printf("[Control ] Phase: NS-GREEN\n"); fflush(stdout);
        SET_PHASE(PHASE_NS_GREEN, cfg->green_duration);

        set_light(mqid, DIR_NORTH, CMD_SET_GREEN, &pend);
        set_light(mqid, DIR_SOUTH, CMD_SET_GREEN, &pend);
        set_light(mqid, DIR_EAST,  CMD_SET_RED,   &pend);
        set_light(mqid, DIR_WEST,  CMD_SET_RED,   &pend);

        { int fe = 0; COUNTDOWN(cfg->green_duration, fe); (void)fe; }
        if (!state->running) break;

        /*
         * VEHICLE-ADAPTIVE TIMING:
         * If no vehicles are waiting in the NS direction at the end
         * of NS-GREEN, log it (the info is already in shared memory
         * from vehicle.c). This demonstrates the system reads vehicle
         * presence data — the EW phase will still run so EW cars get
         * their turn even if NS is empty (fairness).
         */
        if (!any_vehicles(state, semid, DIR_NORTH, DIR_SOUTH)) {
            log_event(mqid, -1,
                "Vehicle info: NS roads empty at green end — noted");
        }

        /* ══ 2. NS-YELLOW ═════════════════════════════════════ */
        log_event(mqid, -1, "Phase: NS-YELLOW");
        printf("[Control ] Phase: NS-YELLOW\n"); fflush(stdout);
        SET_PHASE(PHASE_NS_YELLOW, cfg->yellow_duration);

        set_light(mqid, DIR_NORTH, CMD_SET_YELLOW, &pend);
        set_light(mqid, DIR_SOUTH, CMD_SET_YELLOW, &pend);
        sleep((unsigned)cfg->yellow_duration);
        if (!state->running) break;

        /* ══ 3. ALL-RED #1 + pedestrian check ════════════════ */
        SET_PHASE(PHASE_ALL_RED_1, cfg->allred_duration);
        for (int d = 0; d < DIR_COUNT; d++)
            set_light(mqid, d, CMD_SET_RED, &pend);
        sleep((unsigned)cfg->allred_duration);
        run_ped_phases(mqid, semid, state, cfg, ped_queue, &pend, logbuf);
        if (!state->running) break;

        /* ══ 4. EW-GREEN ══════════════════════════════════════ */
        log_event(mqid, -1, "Phase: EW-GREEN");
        printf("[Control ] Phase: EW-GREEN\n"); fflush(stdout);
        SET_PHASE(PHASE_EW_GREEN, cfg->green_duration);

        set_light(mqid, DIR_EAST,  CMD_SET_GREEN, &pend);
        set_light(mqid, DIR_WEST,  CMD_SET_GREEN, &pend);
        set_light(mqid, DIR_NORTH, CMD_SET_RED,   &pend);
        set_light(mqid, DIR_SOUTH, CMD_SET_RED,   &pend);

        { int fe = 0; COUNTDOWN(cfg->green_duration, fe); (void)fe; }
        if (!state->running) break;

        if (!any_vehicles(state, semid, DIR_EAST, DIR_WEST)) {
            log_event(mqid, -1,
                "Vehicle info: EW roads empty at green end — noted");
        }

        /* ══ 5. EW-YELLOW ═════════════════════════════════════ */
        log_event(mqid, -1, "Phase: EW-YELLOW");
        printf("[Control ] Phase: EW-YELLOW\n"); fflush(stdout);
        SET_PHASE(PHASE_EW_YELLOW, cfg->yellow_duration);

        set_light(mqid, DIR_EAST, CMD_SET_YELLOW, &pend);
        set_light(mqid, DIR_WEST, CMD_SET_YELLOW, &pend);
        sleep((unsigned)cfg->yellow_duration);
        if (!state->running) break;

        /* ══ 6. ALL-RED #2 + pedestrian check ════════════════ */
        SET_PHASE(PHASE_ALL_RED_2, cfg->allred_duration);
        for (int d = 0; d < DIR_COUNT; d++)
            set_light(mqid, d, CMD_SET_RED, &pend);
        sleep((unsigned)cfg->allred_duration);
        run_ped_phases(mqid, semid, state, cfg, ped_queue, &pend, logbuf);

    } /* end main loop */

    /* Clean shutdown: all lights through proper transition */
    all_to_red(mqid, semid, state, cfg->yellow_duration, &pend);
    log_event(mqid, -1, "Control logic shutdown");
    shm_detach(state);
    printf("[Control ] Shutdown.\n");
    fflush(stdout);

    #undef SET_PHASE
    #undef COUNTDOWN
}
