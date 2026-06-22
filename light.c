#include "traffic.h"
#include "shared_state.h"
#include "logger.h"

void run_light(int mqid, int shmid, int semid, int direction) {
    SharedState *state = shm_attach(shmid);
    if (!state) exit(1);

    long my_mtype = MTYPE_FOR_DIR[direction];
    int  cur      = LIGHT_RED;
    char logbuf[128];

    snprintf(logbuf, sizeof logbuf, "Light [%s] started → RED",
             DIR_NAME[direction]);
    log_event(mqid, direction, logbuf);
    printf("[Light %-5s] Started.\n", DIR_NAME[direction]);
    fflush(stdout);

    TrafficMsg m;
    while (1) {
        if (mq_recv(mqid, &m, my_mtype) < 0) {
            if (errno == EIDRM || errno == EINVAL) break;
            continue;
        }
        if (m.cmd == CMD_SHUTDOWN) break;

        int new_state = -1;
        switch (m.cmd) {
            case CMD_SET_RED:    new_state = LIGHT_RED;    break;
            case CMD_SET_YELLOW: new_state = LIGHT_YELLOW; break;
            case CMD_SET_GREEN:  new_state = LIGHT_GREEN;  break;
            default: continue;
        }

        /* Safety: GREEN → RED direct is a violation (only when running) */
        if (new_state == LIGHT_RED && cur == LIGHT_GREEN && state->running) {
            snprintf(logbuf, sizeof logbuf,
                     "SAFETY VIOLATION: [%s] skipped YELLOW (GREEN→RED)",
                     DIR_NAME[direction]);
            log_event(mqid, direction, logbuf);
            sem_lock(semid, SEM_SHM_WRITE);
            state->safety_violations++;
            sem_unlock(semid, SEM_SHM_WRITE);
        }

        int old = cur;
        cur = new_state;

        sem_lock(semid, SEM_SHM_WRITE);
        state->light_state[direction]      = cur;
        state->light_changed_at[direction] = time(NULL);
        sem_unlock(semid, SEM_SHM_WRITE);

        snprintf(logbuf, sizeof logbuf, "Light [%s]: %s → %s",
                 DIR_NAME[direction], LIGHT_NAME[old], LIGHT_NAME[cur]);
        log_event(mqid, direction, logbuf);

        printf("[Light %-5s] %s → %s\n",
               DIR_NAME[direction], LIGHT_NAME[old], LIGHT_NAME[cur]);
        fflush(stdout);

        /* Confirm back to control */
        TrafficMsg reply;
        memset(&reply, 0, sizeof reply);
        reply.mtype     = MTYPE_TO_CONTROL;
        reply.cmd       = EVT_LIGHT_CHANGED;
        reply.direction = direction;
        reply.value     = cur;
        snprintf(reply.info, sizeof reply.info,
                 "%s confirmed %s", DIR_NAME[direction], LIGHT_NAME[cur]);
        mq_send(mqid, &reply);
    }

    shm_detach(state);
    printf("[Light %-5s] Shutdown.\n", DIR_NAME[direction]);
    fflush(stdout);
}
