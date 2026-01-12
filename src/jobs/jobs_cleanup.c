#include"jobs_cleanup.h"

int cleanup_job_struct(t_job* job){

    if(job == NULL){
        return -1;
    }

    job->job_id = -1;

    if(job->command)
        free(job->command);
    job->command = NULL;
    
    if(job->processes){
        t_process* process = job->processes;
        t_process* bomb = NULL;
        while(process != NULL){
            bomb = process;
            process = process->next;
            free(bomb);
            bomb = NULL;
        }
    }
    job->processes = NULL; 

    return 0;
}