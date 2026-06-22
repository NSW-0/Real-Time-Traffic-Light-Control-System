#include "shared_state.h"

union semun { int val; struct semid_ds *buf; unsigned short *array; };

int shm_create(void) {
    int shmid = shmget(IPC_KEY_SHM, sizeof(SharedState),
                       IPC_CREAT | IPC_EXCL | 0666);
    if (shmid < 0) {
        int old = shmget(IPC_KEY_SHM, sizeof(SharedState), 0666);
        if (old >= 0) shmctl(old, IPC_RMID, NULL);
        shmid = shmget(IPC_KEY_SHM, sizeof(SharedState),
                       IPC_CREAT | IPC_EXCL | 0666);
        if (shmid < 0) { perror("shmget"); return -1; }
    }
    SharedState *s = shm_attach(shmid);
    if (!s) return -1;
    memset(s, 0, sizeof *s);
    for (int i = 0; i < DIR_COUNT; i++) {
        s->light_state[i]      = LIGHT_RED;
        s->light_changed_at[i] = time(NULL);
    }
    s->current_phase       = PHASE_ALL_RED_1;
    s->running             = 1;
    s->emergency_active    = 0;
    s->emergency_direction = -1;
    s->ped_active          = 0;
    s->ped_direction       = -1;
    shm_detach(s);
    return shmid;
}

SharedState *shm_attach(int shmid) {
    void *p = shmat(shmid, NULL, 0);
    if (p == (void *)-1) { perror("shmat"); return NULL; }
    return (SharedState *)p;
}
void shm_detach(SharedState *s) { if (s) shmdt(s); }
void shm_destroy(int shmid)     { shmctl(shmid, IPC_RMID, NULL); }

int sem_create(void) {
    int semid = semget(IPC_KEY_SEM, SEM_SHM_COUNT,
                       IPC_CREAT | IPC_EXCL | 0666);
    if (semid < 0) {
        int old = semget(IPC_KEY_SEM, SEM_SHM_COUNT, 0666);
        if (old >= 0) semctl(old, 0, IPC_RMID);
        semid = semget(IPC_KEY_SEM, SEM_SHM_COUNT,
                       IPC_CREAT | IPC_EXCL | 0666);
        if (semid < 0) { perror("semget"); return -1; }
    }
    union semun u; u.val = 1;
    semctl(semid, SEM_SHM_WRITE, SETVAL, u);
    return semid;
}
void sem_destroy(int semid) { semctl(semid, 0, IPC_RMID); }
