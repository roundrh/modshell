#ifndef JOBS_H
#define JOBS_H

#include<stdio.h>
#include<unistd.h>
#include<stdlib.h>

typedef enum e_state {

    S_NONE = 0,
    S_RUNNING,
    S_COMPLETED,
    S_STOPPED
} t_state;

typedef enum e_position {

    P_NONE = 0,
    P_FOREGROUND,
    P_BACKGROUND
} t_position;

typedef struct s_process {

    pid_t pid;
    int exit_status;
    int completed;
    int stopped;
    int running;
    struct s_process* next;
} t_process;

typedef struct s_job{

    int job_id;
    pid_t pgid;
    t_position position;
    t_state state;
    char* command;
    t_process* processes;

    int last_exit_status;

    int process_count;

} t_job;

#endif

