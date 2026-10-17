#include "job.h"

extern struct job_manager *manager;
/*--------------------------------------------------------------------*/
void init_job_manager() {
	manager = (struct job_manager *)calloc(1, sizeof(struct job_manager));
	if (manager == NULL) {
		fprintf(stderr, "[Error] job manager allocation failed\n");
		exit(EXIT_FAILURE);
	}
    manager->n_jobs = 0;
    memset(manager->jobs, 0, sizeof(manager->jobs));
}
/*--------------------------------------------------------------------*/
struct job *find_job_by_jid(int job_id) {


    for (int i = 0; i < MAX_JOBS; i++){
        if (manager->jobs[i].job_id == job_id) return &manager->jobs[i];
    }
    return NULL;
}
/*--------------------------------------------------------------------*/
int remove_pid_from_job(struct job *job, pid_t pid) {

    for (int i = 0; i < job->n_pids; i++) {
        if (job->pids[i] == pid) {
            job->pids[i] = job->pids[--job->n_pids];
            job->remaining_processes--;
            return 1;
        }
    }
    return 0;
}
/*--------------------------------------------------------------------*/
int delete_job(int jobid) {
	
    for (int i = 0; i < MAX_JOBS; i++) {
        if (manager->jobs[i].job_id == jobid) {
            memset(&manager->jobs[i], 0, sizeof(struct job));
            manager->n_jobs--;
            return 1;
        }
    }
    return 0;
}
/*--------------------------------------------------------------------*/
int register_job(pid_t pgid, job_state state){
    if (manager->n_jobs >= MAX_JOBS) {
        fprintf(stderr, "job table full\n");
        return -1;
    }

    int unique_id = 1;
    for (int j = 0; j < MAX_JOBS; j++)
        if (manager->jobs[j].job_id >= unique_id)
            unique_id = manager->jobs[j].job_id + 1;

    for (int i = 0; i < MAX_JOBS; i++) {
        if (manager->jobs[i].job_id == 0) {
            manager->jobs[i].job_id = unique_id;
            manager->jobs[i].pgid = pgid;
            manager->jobs[i].pids[0] = pgid; 
            manager->jobs[i].n_pids = 1;
            manager->jobs[i].remaining_processes = 1;
            manager->jobs[i].state = state;
            manager->n_jobs++;
            return unique_id;
        }
    }
    return -1;
}

/*--------------------------------------------------------------------*/
struct job *find_job_by_pgid(pid_t pgid){
    for (int i = 0; i < MAX_JOBS; i++)
        if (manager->jobs[i].pgid == pgid)
            return &manager->jobs[i];
    return NULL;
}

/*--------------------------------------------------------------------*/
int add_pid_to_job(int jobid, pid_t pid) {
    struct job *job = find_job_by_jid(jobid);
    if (!job) return -1;
    job->pids[job->n_pids++] = pid;
    job->remaining_processes++;
    return 0;
}
