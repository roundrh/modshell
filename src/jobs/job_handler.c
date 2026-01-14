#include"job_handler.h"

static int resize_job_table(t_job*** job_table, size_t *job_table_cap){

    size_t old_cap = *job_table_cap;
    size_t new_cap = old_cap * BUF_GROWTH_FACTOR;
    if(new_cap > MAX_JOBS){
        return -1;
    }

    t_job** new_job_table = realloc(*job_table, sizeof(t_job*) * new_cap);
    if(!new_job_table){
        perror("realloc job table");
        return -1;
    }

    *job_table = new_job_table;
    
    for (size_t i = old_cap; i < new_cap; i++) {
        (*job_table)[i] = NULL;
    }
    
    *job_table_cap = new_cap;
    return 0;
}

int add_job(t_shell* shell, t_job* job){

    if (!shell || !job) {
        return -1;
    }

    if(shell->job_count >= shell->job_table_cap){
        if(resize_job_table(&(shell->job_table), &shell->job_table_cap) == -1){
            fprintf(stderr, "\n Job table full");
            return -1;
        }
    }

    size_t i = 0;
    for (i = 0; i < shell->job_table_cap; i++){
        if(shell->job_table[i] == NULL)  
            break;
    }
    
    shell->job_table[i] = job;
    shell->job_count++;

    job->job_id = shell->job_count;

    return 0;
}

int del_job(t_shell* shell, int job_id){

    if (!shell || job_id <= 0) 
        return -1;

    size_t i;
    int found = 0;
    
    for (i = 0; i < shell->job_table_cap; i++){

        if(shell->job_table[i] == NULL)
            continue;

        if(shell->job_table[i]->job_id == job_id) {
            found = 1;
            break;
        }
    }

    if (found == 0) {
        return -1;
    }

    cleanup_job_struct(shell->job_table[i]);

    free(shell->job_table[i]);
    shell->job_table[i] = NULL;
    
    return 0;
}

int is_job_completed(t_job *job) {

    if (!job) 
        return -1;

    t_process *p = job->processes;
    while (p) {
        if (p->completed == 0) return 0;
        p = p->next;
    }

    return 1;
}

int is_job_stopped(t_job* job){

    if (!job) return -1;

    t_process *p = job->processes;
    while (p) {
        if (p->stopped == 1) return 1;
        p = p->next;
    }
    return 0;
}

int add_process_to_job(t_job *job, t_process* process){

    if (!job) return -1;

    t_process* head = job->processes;
    if(head == NULL){
        
        job->processes = process;
        job->process_count++;

        return 0;
    } else{
        while(head->next != NULL){
            head = head->next;
        }
        head->next = process;

        job->process_count++;

        return 0;
    }
    return -1;
}

int mark_job_state(t_shell* shell, int job_id, t_state state){

    for (size_t i = 0; i < shell->job_table_cap; i++){
        if(shell->job_table[i] == NULL)
            continue;
        
        if(shell->job_table[i]->job_id == job_id){
            shell->job_table[i]->state = state;
            return 0;
        }
    }

    return -1;
}

int move_job_position(t_shell* shell, int job_id, t_position pos){

    for (size_t i = 0; i < shell->job_table_cap; i++){
        
        if(shell->job_table[i] == NULL)
            continue;

        if(shell->job_table[i]->job_id == job_id){
            shell->job_table[i]->position = pos;
            return 0;
        }
    }

    return -1;
}

int is_job_table_empty(t_shell* shell){
    
    for(size_t i = 0; i < shell->job_table_cap; i++){
        if(shell->job_table[i] != NULL)
            return 0;
    }

    return 1;
}

t_job* get_foreground_job(t_shell* shell){

    for (size_t i = 0; i < shell->job_table_cap; i++){
        if(shell->job_table[i] == NULL) 
            continue;

        if(shell->job_table[i]->position == P_FOREGROUND){
            return shell->job_table[i];
        }
    }

    return NULL;
}
t_job* find_job(t_shell* shell, int job_id){

    for (size_t i = 0; i < shell->job_table_cap; i++){
        if(shell->job_table[i] == NULL) 
            continue;

        if(shell->job_table[i]->job_id == job_id){
            return shell->job_table[i];
        }
    }
    return NULL;
}

t_job* find_job_by_pid(t_shell* shell, pid_t pid){

    for(size_t i = 0; i < shell->job_table_cap; i++){
        if(shell->job_table[i] == NULL)
            continue;

        t_job* in_job = shell->job_table[i];
        t_process* process = in_job->processes;
        while(process){
            if(process->pid == pid){
                return in_job;
            }
            process = process->next;
        }
    }    

    return NULL;
}

t_process* find_process_in_job(t_job* job, pid_t pid){
    t_process* process = job->processes;
    while(process){

        if(process->pid == pid){
            return process;
        }
        process = process->next;
    }

    return NULL;
}

void print_job_info(t_job* job){

    if(!job) 
        return;

    if(job->state == S_RUNNING)
        printf("[%d] %d - Running\n", job->job_id, job->pgid);
    if(job->state == S_STOPPED)
        printf("[%d] %d - Stopped\n", job->job_id, job->pgid);
    if(job->state == S_COMPLETED)
        printf("[%d] %d - Completed\n", job->job_id, job->pgid);
}