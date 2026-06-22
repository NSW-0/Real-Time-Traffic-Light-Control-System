#ifndef SHARED_STATE_H
#define SHARED_STATE_H

#include "traffic.h"

/*
 * SharedState lives in System V shared memory.
 * All processes map the same block — no extra IPC needed
 * for display to read the current intersection state.
 *
 * PROTECTION: all writes must use sem_lock/sem_unlock.
 */
typedef struct {
    /* 4 light states — one per direction */
    int    light_state[DIR_COUNT];
    time_t light_changed_at[DIR_COUNT];

    /* Phase info */
    int    current_phase;
    int    phase_seconds_left;
    int    phase_total;

    /* Pedestrian */
    int    ped_request[DIR_COUNT];
    int    ped_active;
    int    ped_direction;
    time_t ped_request_time[DIR_COUNT];

    /* Vehicle detection */
    int    vehicles_waiting[DIR_COUNT];

    /* Emergency */
    int    emergency_active;
    int    emergency_direction;
    time_t emergency_start;

    /* System */
    int    running;
    int    safety_violations;
} SharedState;

int          shm_create(void);
SharedState *shm_attach(int shmid);
void         shm_detach(SharedState *s);
void         shm_destroy(int shmid);
int          sem_create(void);
void         sem_destroy(int semid);

#endif
