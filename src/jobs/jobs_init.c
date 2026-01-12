#include "jobs_init.h"

int init_job_struct(t_job* job){

    if(job == NULL)
        return -1;
    
    job->job_id = -1;
    job->pgid = -1;
    job->position = P_NONE;
    job->state = S_NONE;
    job->command = NULL;
    job->processes = NULL;
    job->process_count = 0;

    job->last_exit_status = -1;

    return 0;
}