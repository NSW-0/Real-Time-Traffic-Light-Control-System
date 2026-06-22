#include "traffic.h"
#include "shared_state.h"
#include "logger.h"

void run_vehicle_detector(int mqid, int shmid, int semid, int vehicle_interval) {
    SharedState *state = shm_attach(shmid);
    if (!state) exit(1);
    srand((unsigned)(time(NULL) ^ (unsigned long)getpid()));
    printf("[Vehicles] Detector started (interval=%ds).\n", vehicle_interval);
    fflush(stdout);
    log_event(mqid, -1, "Vehicle detector started");

    char logbuf[128];
    while (state->running) {
        sleep((unsigned)vehicle_interval);
        if (!state->running) break;

        int dir   = rand() % DIR_COUNT;
        int count = rand() % 6;

        sem_lock(semid, SEM_SHM_WRITE);
        state->vehicles_waiting[dir] = count;
        sem_unlock(semid, SEM_SHM_WRITE);

        TrafficMsg m;
        memset(&m, 0, sizeof m);
        m.mtype     = MTYPE_TO_CONTROL;
        m.cmd       = EVT_VEHICLE_DETECTED;
        m.direction = dir;
        m.value     = count;
        snprintf(m.info, sizeof m.info, "Vehicle sensor [%s]: %d car(s)",
                 DIR_NAME[dir], count);
        mq_send(mqid, &m);

        snprintf(logbuf, sizeof logbuf,
                 "Vehicle sensor [%s]: %d car(s)", DIR_NAME[dir], count);
        log_event(mqid, dir, logbuf);
        printf("[Vehicles] %s: %d car(s) waiting\n", DIR_NAME[dir], count);
        fflush(stdout);
    }

    shm_detach(state);
    printf("[Vehicles] Detector shutdown.\n");
    fflush(stdout);
}
