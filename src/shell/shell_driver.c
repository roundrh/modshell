#include<stdlib.h>
#include<stdio.h>
#include<unistd.h>
#include"userinp.h"
#include"shell_init.h"
#include"shell_cleanup.h"
#include"executor.h"

static t_shell* g_shell_ptr = NULL;
static char* g_cmd_buf_ptr = NULL;

static int reap_sigchld_jobs(t_shell* shell){

    sigset_t block_mask, old_mask;
    int reap_flag = 1;
    if(sigemptyset(&block_mask) == -1){
        perror("sigemptyset");
        reap_flag = 0;
    }
    if(sigaddset(&block_mask, SIGCHLD) == -1){
        perror("sigaddset");
        reap_flag = 0;  
    }

    if(sigprocmask(SIG_BLOCK, &block_mask, &old_mask) == -1){
        perror("sigproc");
        reap_flag = 0;
    }

    size_t i = 0;
    t_job* job = NULL;

    if(!reap_flag){
        return -1;
    }

    while (i < shell->job_table_cap) {
        job = shell->job_table[i];
        if (!job) {
            i++;
            continue;
        }

        int status;
        pid_t pid;

        while ((pid = waitpid(-job->pgid, &status, WNOHANG)) > 0) {

            t_process* process = find_process_in_job(job, pid);
            if (!process) 
                break;

            if (WIFEXITED(status) || WIFSIGNALED(status)) {
                process->completed = 1;
                process->stopped = 0;
                process->running = 0;
            } else if (WIFSTOPPED(status)) {
                process->stopped = 1;
                process->completed = 0;
                process->running = 0;
            } else if (WIFCONTINUED(status)) {
                process->running = 1;
                process->stopped = 0;
                process->completed = 0;
            }
        }

        if (job && is_job_completed(job) && job->position == P_BACKGROUND) {

            job->state = S_COMPLETED;
            printf("[+] Job completed: %d\n", job->job_id);
            del_job(shell, job->job_id);
        } else if (job && is_job_stopped(job)) {

            job->state = S_STOPPED;
            job->position = P_BACKGROUND;
            printf("\n[-] Job stopped: %d", job->job_id);
        } else if(job && job->position == P_BACKGROUND){

            job->state = S_RUNNING;
            //printf("\n[>] Job running: %d", job->job_id);
        }

        i++;
    }

    if(sigprocmask(SIG_SETMASK, &old_mask, NULL) == -1){
        perror("sigproc");
    }
    
    /* Enforcer */
    if(is_job_table_empty(shell)){
        shell->job_count = 0;
    }
    return 0;
}


void set_global_shell_ptr(t_shell* ptr){
    g_shell_ptr = ptr;
}
void set_global_cmd_buf_ptr(char* ptr){
    g_cmd_buf_ptr = ptr;
}

void cleanup_global_shell_ptr(void){
    if(g_shell_ptr != NULL){
        cleanup_shell(g_shell_ptr, 0);
        g_shell_ptr = NULL;
    }
}
void cleanup_global_cmd_buf_ptr(void){
    if(g_cmd_buf_ptr != NULL){
        free(g_cmd_buf_ptr);
        g_cmd_buf_ptr = NULL;
    }
}


static int replace_home_dir(char** buf) {

    if (strncmp(*buf, "/home/", 6) != 0)
        return -1;

    char* replacement = strdup(*buf);
    if (!replacement) {
        perror("115: strdup malloc error");
        return -1;
    }

    char* bufbuf = malloc(PATH_MAX);
    if (!bufbuf) {
        perror("121: bufbuf malloc error");
        free(replacement);
        return -1;
    }
    bufbuf[0] = '~';
    bufbuf[1] = '\0';

    char* part = strtok(replacement, "/");   // ""
    part = strtok(NULL, "/");                // home

    while ((part = strtok(NULL, "/")) != NULL) {
        strcat(bufbuf, "/");
        strcat(bufbuf, part);
    }
    for(int i = 0; i < strlen(*buf); i++) (*buf)[i] = '\0'; ///< Clear buf
    free(*buf);
    *buf = strdup(bufbuf);
    free(bufbuf);
    free(replacement);

    return 0;
}

int main(int argc, char** argv){

    t_shell shell_state;
    char* cmd_line_buf = NULL;

    set_global_shell_ptr(&shell_state);
    set_global_cmd_buf_ptr(cmd_line_buf);

    atexit(cleanup_global_shell_ptr);
    atexit(cleanup_global_cmd_buf_ptr);

    if(init_shell_state(&shell_state) == -1){
        perror("shell state init fatal fail");
        exit(1);
    }

    if (argc > 1) {

        FILE *script = fopen(argv[1], "r");
        if (!script) {
            perror("msh: fail to open");
            exit(1);
        }

        size_t cap = 0;
        char* line = NULL;
        ssize_t n;
        n = getline(&line, &cap, script);
        if (n != -1 && line[0] == '#' && line[1] == '!') {
            free(line);
            line = NULL;
        }

        while ((n = getline(&line, &cap, script)) != -1) {
            line[strcspn(line, "\n")] = '\0';
            line[strcspn(line, "\r")] = '\0';
            
            char* trimmed = line;
            while (*trimmed && (*trimmed == ' ' || *trimmed == '\t')) trimmed++;
            if (*trimmed == '\0') { 
                free(line);
                line = NULL;
                continue; 
            }

            parse_and_execute(&line, &shell_state, &shell_state.token_stream);

            free(line);
            line = NULL;
        }

        free(line);
        fclose(script);
        cleanup_shell(&shell_state, shell_state.last_exit_status);
        exit(shell_state.last_exit_status);
    }

    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    while (1) {
        if (shell_state.is_interactive) {
            if(shell_state.job_control_flag && sigchld_flag){
                sigchld_flag = 0;
                reap_sigchld_jobs(&shell_state);
            }

            if (shell_state.job_control_flag && tcgetpgrp(shell_state.tty_fd) != shell_state.pgid) {
                tcsetpgrp(shell_state.tty_fd, shell_state.pgid);
            }

            char hostname[256];
            if (gethostname(hostname, sizeof(hostname)) == -1) {
                perror("gethostname");
            } else {
                hostname[sizeof(hostname)-1] = '\0';
            }

            reset_terminal_mode(&shell_state);
            HANDLE_WRITE_FAIL_FATAL(shell_state.tty_fd, "\033[?25h", 6, cmd_line_buf);

            char* dir = getcwd(NULL, 0);
            if (!dir) { perror("getcwd"); exit(1); }
            replace_home_dir(&dir);
            char* user = getenv("USER");
            printf("\n\033[1;37m%s@%s %s\033[0m:\033[0;37m%s\033[0m \033[1;37m$ \033[0m", 
                user, hostname, dir, shell_state.sh_name);
            free(dir);

            rawify(&shell_state);
            cmd_line_buf = read_user_inp(&shell_state);
            set_global_cmd_buf_ptr(cmd_line_buf);
            reset_terminal_mode(&shell_state);
        } else {
            size_t cap = 0;
            ssize_t n = getline(&cmd_line_buf, &cap, stdin);
            if (n == -1) {
                cleanup_shell(&shell_state, shell_state.last_exit_status);
                free(cmd_line_buf);
                exit(shell_state.last_exit_status);
            }
            set_global_cmd_buf_ptr(cmd_line_buf);
        }

        if (!cmd_line_buf || *cmd_line_buf == '\0' || *cmd_line_buf == '\n' || *cmd_line_buf == '\r') {
            if (shell_state.is_interactive)
                HANDLE_WRITE_FAIL_FATAL(shell_state.tty_fd, "\n", 1, cmd_line_buf);

            reap_sigchld_jobs(&shell_state);
            free(cmd_line_buf);
            set_global_cmd_buf_ptr(NULL);
            continue;
        }

        cmd_line_buf[strcspn(cmd_line_buf, "\n")] = '\0';
        cmd_line_buf[strcspn(cmd_line_buf, "\r")] = '\0';

        if(push_front_dll(cmd_line_buf, &(shell_state.history)) == NULL){
            perror("fatal fail pushfrontdll");
            return -1;
        }

        if (shell_state.is_interactive)
            HANDLE_WRITE_FAIL_FATAL(shell_state.tty_fd, "\n", 1, cmd_line_buf);

        parse_and_execute(&cmd_line_buf, &shell_state, &shell_state.token_stream);
        set_global_cmd_buf_ptr(cmd_line_buf);
        
        free(cmd_line_buf);
        set_global_cmd_buf_ptr(NULL);
        for(int i = 0; i < shell_state.token_stream.tokens_arr_len; i++){
            shell_state.token_stream.tokens[i].start = NULL;
            shell_state.token_stream.tokens[i].len = 0;
            shell_state.token_stream.tokens[i].type = -1;
        }
    }

    return 0;
}