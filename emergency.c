#include "traffic.h"
#include "shared_state.h"
#include "logger.h"

void run_emergency(int mqid, int shmid, int semid, int emg_interval) {
    (void)semid;
    SharedState *state = shm_attach(shmid);
    if (!state) exit(1);
    srand((unsigned)(time(NULL) ^ ((unsigned long)getpid() << 4)));
    printf("[Emergency] Process started (interval=%ds).\n", emg_interval);
    fflush(stdout);
    log_event(mqid, -1, "Emergency handler started");

    char logbuf[128];
    while (state->running) {
        sleep((unsigned)emg_interval);
        if (!state->running) break;

        int dir = rand() % DIR_COUNT;

        snprintf(logbuf, sizeof logbuf,
                 "EMERGENCY: vehicle detected from %s", DIR_NAME[dir]);
        log_event(mqid, dir, logbuf);
        printf("\033[1;31m[Emergency] *** Emergency vehicle from %s! ***\033[0m\n",
               DIR_NAME[dir]);
        fflush(stdout);

        TrafficMsg m;
        memset(&m, 0, sizeof m);
        m.mtype     = MTYPE_TO_CONTROL;
        m.cmd       = EVT_EMERGENCY_ON;
        m.direction = dir;
        snprintf(m.info, sizeof m.info,
                 "Emergency vehicle from %s", DIR_NAME[dir]);
        mq_send(mqid, &m);

        sleep(15);   /* vehicle passes through */
        if (!state->running) break;

        memset(&m, 0, sizeof m);
        m.mtype     = MTYPE_TO_CONTROL;
        m.cmd       = EVT_EMERGENCY_OFF;
        m.direction = dir;
        snprintf(m.info, sizeof m.info, "Emergency cleared from %s", DIR_NAME[dir]);
        mq_send(mqid, &m);

        snprintf(logbuf, sizeof logbuf,
                 "Emergency cleared from %s", DIR_NAME[dir]);
        log_event(mqid, dir, logbuf);
        printf("[Emergency] Cleared. Normal cycle resuming.\n");
        fflush(stdout);
    }

    shm_detach(state);
    printf("[Emergency] Shutdown.\n");
    fflush(stdout);
}
