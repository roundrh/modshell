#ifndef JOB_HANDLER_H
#define JOB_HANDLER_H

#include<errno.h>
#include"jobs.h"
#include"jobs_init.h"
#include"jobs_cleanup.h"
#include"shell.h"

#define MAX_JOBS 8192
#define BUF_GROWTH_FACTOR 2
#define INITIAL_JOB_TABLE_LENGTH 32

int add_job(t_shell* shell, t_job* job);
int del_job(t_shell* shell, int job_id, bool flow);

int add_process_to_job(t_job *job, t_process* process);

int reset_job_table_cap(t_shell* shell);

int mark_job_state(t_shell* shell, int job_id, t_state state);

int move_job(t_shell* shell, int job_id, t_position pos);

int is_job_table_empty(t_shell* shell);
int is_job_table_full(t_shell* shell);

int is_job_completed(t_job* job);
int is_job_stopped(t_job* job);

t_job* get_foreground_job(t_shell* shell);
t_job* find_job(t_shell* shell, int job_id);
t_job* find_job_by_pid(t_shell* shell, pid_t pid);
t_process* find_process_in_job(t_job* job, pid_t pid);

void print_job_info(t_job* job);

#endif // ! JOB_HANDLER_H
