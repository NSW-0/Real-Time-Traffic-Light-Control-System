#include "traffic.h"
#include "shared_state.h"
#include "logger.h"

void run_pedestrian(int mqid, int shmid, int semid,
                    int ped_interval, int ped_max_wait) {
    SharedState *state = shm_attach(shmid);
    if (!state) exit(1);
    srand((unsigned)(time(NULL) ^ ((unsigned long)getpid() << 2)));

    printf("[Pedestrian] Process started (interval=%ds, max_wait=%ds).\n",
           ped_interval, ped_max_wait);
    fflush(stdout);
    log_event(mqid, -1, "Pedestrian process started");

    char logbuf[128];
    int  tick = 0;

    while (state->running) {
        sleep(1);
        tick++;
        if (!state->running) break;

        /* Check for max-wait violations */
        for (int d = 0; d < DIR_COUNT; d++) {
            if (!state->ped_request[d]) continue;
            double waited = difftime(time(NULL), state->ped_request_time[d]);
            if ((int)waited > ped_max_wait) {
                snprintf(logbuf, sizeof logbuf,
                         "TIMING VIOLATION: Pedestrian [%s] waited %.0fs (max=%ds)",
                         DIR_NAME[d], waited, ped_max_wait);
                log_event(mqid, d, logbuf);
                printf("\033[1;33m[Pedestrian] TIMING VIOLATION: "
                       "%s waited too long!\033[0m\n", DIR_NAME[d]);
                fflush(stdout);
            }
        }

        /* Generate a new request every ped_interval seconds */
        if (tick % ped_interval != 0) continue;

        int dir = rand() % DIR_COUNT;
        if (state->ped_request[dir]) continue;  /* already pending */

        sem_lock(semid, SEM_SHM_WRITE);
        state->ped_request[dir]      = 1;
        state->ped_request_time[dir] = time(NULL);
        sem_unlock(semid, SEM_SHM_WRITE);

        TrafficMsg m;
        memset(&m, 0, sizeof m);
        m.mtype     = MTYPE_TO_CONTROL;
        m.cmd       = EVT_PEDESTRIAN_REQ;
        m.direction = dir;
        snprintf(m.info, sizeof m.info,
                 "Pedestrian crossing request: %s", DIR_NAME[dir]);
        mq_send(mqid, &m);

        snprintf(logbuf, sizeof logbuf,
                 "Pedestrian request: %s crossing", DIR_NAME[dir]);
        log_event(mqid, dir, logbuf);
        printf("[Pedestrian] Request: %s crossing\n", DIR_NAME[dir]);
        fflush(stdout);
    }

    shm_detach(state);
    printf("[Pedestrian] Shutdown.\n");
    fflush(stdout);
}
