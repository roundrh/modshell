/**
 * @file builtins.c
 * @brief Implementation of shell builtin commands
 */

#include"builtins.h"

static int update_no_noti_jobs(t_shell* shell){

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
        } else if (job && is_job_stopped(job)) {
            job->state = S_STOPPED;
            job->position = P_BACKGROUND;
        } else if(job && job->position == P_BACKGROUND){
            job->state = S_RUNNING;
        }

        i++;
    }

    if(sigprocmask(SIG_SETMASK, &old_mask, NULL) == -1){
        perror("sigproc");
    }
    
    if(is_job_table_empty(shell)){
        if(reset_job_table_cap(shell) == -1){
            exit(EXIT_FAILURE);
        }
        shell->job_count = 0;
    }
    return 0;
}
static void cleanup_argv(char** argv){

    if(argv == NULL)
        return;

    int i = 0;
    while(argv[i] != NULL){

        free(argv[i]);
        argv[i] = NULL;

        i++;
    }

    free(argv);
    argv = NULL;
}

static int realloc_env_shell(t_shell* shell){
    
    size_t new_cap = shell->env_cap * BUF_GROWTH_FACTOR;
    char** new_shell_env = realloc(shell->env, (new_cap) * sizeof(char*));
    if(!new_shell_env){
        perror("fatal malloc");
        return -1;
    }

    shell->env = new_shell_env;

    for(size_t j = shell->env_cap; j < new_cap; j++){
        shell->env[j] = NULL;
    }

    shell->env_cap = new_cap;

    return 0;
}

static int getenv_local(char** env, const char* var_name){

    if (!env || !var_name || !*var_name)
        return -2;

    size_t key_len = strlen(var_name);

    for (int i = 0; env[i]; i++) {
        if (strncmp(env[i], var_name, key_len) == 0 && env[i][key_len] == '=') {
            return i;
        }
    }
    return -1;
}
static int realloc_argv(char*** argv, size_t* argv_cap){

    size_t new_cap = *argv_cap * BUF_GROWTH_FACTOR;

    char** new_argv = realloc(*argv, new_cap * sizeof(char*));
    if(!new_argv)
        return -1;

    for(int i = *argv_cap; i < new_cap; i++)
        new_argv[i] = NULL;

    *argv = new_argv;
    *argv_cap = new_cap;

    return 0;
}
static int add_to_env(t_shell* shell, char* var, char* val){

    if (!shell || !var || !val)
        return -1; 

    if(shell->env_count + 1 >= shell->env_cap){
        if(realloc_env_shell(shell) == -1){
            perror("realloc");
            return -1;
        }
    }

    size_t size_env_var = strlen(var) + strlen(val) + 1 + 1; ///< +1 '=' & +1 '\0'
    char* env_var = (char*)malloc(sizeof(char) * size_env_var);
    if(!env_var){
        perror("41: fatal malloc env_var");
        return -1;
    }

    snprintf(env_var, size_env_var, "%s=%s", var, val);
    int idx = -1;
    if( (idx = getenv_local(shell->env, var)) == -1 ) {

        if(shell->env_count - 1 >= shell->env_cap){
            if(realloc_argv(&shell->env, &shell->env_cap) == -1){
                perror("realloc");
                exit(EXIT_FAILURE);
            }
        }
        shell->env[shell->env_count++] = env_var;
        shell->env[shell->env_count] = NULL;
    } else{
        free(shell->env[idx]);
        shell->env[idx] = env_var;
    }
    return 0;
}
static int remove_from_env(t_shell* shell, char* remove){

    for(size_t i = 0; i < shell->env_count; i++){
        if(strncmp(shell->env[i], remove, strlen(remove)) == 0 
        && shell->env[i][strlen(remove)] == '='){

            free(shell->env[i]);

            if (i < shell->env_count - 1) {
                memmove(&shell->env[i], &shell->env[i + 1], (shell->env_count - i - 1) * sizeof(char*));
            }

            shell->env[shell->env_count - 1] = NULL;
            shell->env_count--;

            return 0;
        }
    }

    return -1;
}

static int wait_for_foreground_job(t_job* job, t_shell* shell) {

    int status;
    pid_t pid;
    while(!is_job_completed(job)){

        pid = waitpid(-job->pgid, &status, WUNTRACED | WCONTINUED);
        if(pid <= 0){
            if(errno == EINTR) continue;
            break;
        }

        t_process* process = find_process_in_job(job, pid);
        if(!process) continue;

        if(WIFEXITED(status) == 1 || WIFSIGNALED(status) == 1){
            process->stopped = 0;
            process->running = 0;
            process->completed = 1;
        } else if(WIFSTOPPED(status) == 1){
            process->stopped = 1;
            process->running = 0;
            process->completed = 0;
            job->state = S_STOPPED;

            print_job_info(job);
            return -1;
        } else if(WIFCONTINUED(status) == 1){
            process->stopped = 0;
            process->running = 1;
            process->completed = 0;
        }

        if(is_job_completed(job) == 1){

            del_job(shell, job->job_id, false);
            return 0;
        }
    }

    return 0;
}

/**
 * @brief Builtin help command - displays available commands
 * @param node AST node containing command arguments
 * @param shell Shell context
 * @return Always returns 0
 */
int help_builtin(t_ast_n* node, t_shell* shell, char** argv) {
    const int CMD_WIDTH = 30;

    printf("\n\x1b[1;35m--- List of Built-in Shell Commands ---\x1b[0m\n");

    printf("\x1b[1;36m%-*s\x1b[1;37m%s\n\x1b[0m", CMD_WIDTH, "cd [DIR]", "Change the current directory.");
    printf("\x1b[1;36m%-*s\x1b[1;37m%s\n\x1b[0m", CMD_WIDTH, "pwd", "Print the name of the current working directory.");
    printf("\x1b[1;36m%-*s\x1b[1;37m%s\n\x1b[0m", CMD_WIDTH, "clear", "Clear the terminal screen.");
    printf("\x1b[1;36m%-*s\x1b[1;37m%s\n\x1b[0m", CMD_WIDTH, "exit", "Terminate the shell session.");

    printf("\n\x1b[1;35m--- Variable and Alias Management ---\x1b[0m\n");
    printf("\x1b[1;36m%-*s\x1b[1;37m%s\n\x1b[0m", CMD_WIDTH, "export [VNAME] [VAL]", "Set an environment variable.");
    printf("\x1b[1;36m%-*s\x1b[1;37m%s\n\x1b[0m", CMD_WIDTH, "set [VNAME]", "Set an environment variable.");
    printf("\x1b[1;36m%-*s\x1b[1;37m%s\n\x1b[0m", CMD_WIDTH, "unset [VNAME]", "Remove an environment variable.");
    printf("\x1b[1;36m%-*s\x1b[1;37m%s\n\x1b[0m", CMD_WIDTH, "alias [ALIAS] [CMD]", "Create a command alias (use single quotes for commands with spaces).");
    printf("\x1b[1;36m%-*s\x1b[1;37m%s\n\x1b[0m", CMD_WIDTH, "alias [ALIAS]='[CMD]'", "Alternative alias syntax for complex commands.");
    printf("\x1b[1;36m%-*s\x1b[1;37m%s\n\x1b[0m", CMD_WIDTH, "unalias [ALIAS]", "Remove a defined alias.");

    printf("\n\x1b[1;35m--- System and Utility Commands ---\x1b[0m\n");
    printf("\x1b[1;36m%-*s\x1b[1;37m%s\n\x1b[0m", CMD_WIDTH, "whoami", "Print the effective username.");
    printf("\x1b[1;36m%-*s\x1b[1;37m%s\n\x1b[0m", CMD_WIDTH, "uname opt[-s/-n/-r/-v/-m/-d]", "Print system information (name, version, etc.).");
    printf("\x1b[1;36m%-*s\x1b[1;37m%s\n\x1b[0m", CMD_WIDTH, "sleep [time][s/m/h]", "Pause execution for a specified duration (seconds, minutes, or hours).");

    printf("\n\x1b[1;35m--- I/O Redirection ---\x1b[0m\n");
    printf("\x1b[1;36m%-*s\x1b[1;37m%s\n\x1b[0m", CMD_WIDTH, "[CMD] > [filename]", "Truncate output to file, create if not found.");
    printf("\x1b[1;36m%-*s\x1b[1;37m%s\n\x1b[0m", CMD_WIDTH, "[CMD] >> [filename]", "Append output to file, create if not found.");
    printf("\x1b[1;36m%-*s\x1b[1;37m%s\n\x1b[0m", CMD_WIDTH, "[CMD] < [filename]", "Input from file to command.");
    printf("\x1b[1;36m%-*s\x1b[1;37m%s\n\x1b[0m", CMD_WIDTH, "[CMD] << [filename]", "Heredoc EOF.");

    printf("\n");

    fflush(stdout);
    return 0;
}

/**
 * @brief Builtin cd command - change directory
 * @param node AST node containing command arguments
 * @param shell Shell context
 * @return 0 on success, 1 on failure
 */
int cd_builtin(t_ast_n* node, t_shell* shell, char** argv){

    if(argv[1] == NULL){
        char* ptr = getenv("HOME");
        if(!ptr){
            perror("208: err getting home dir");
            return -1;
        }
        int chdir_status = chdir(ptr);
        if(chdir_status == -1){
            fprintf(stderr, "chdir fail\n");
            return -1;
        }
    } else {
        char* ptr = argv[1];
        int chdir_status = chdir(ptr);
        if(chdir_status == -1){
            fprintf(stderr, "msh: cd: file not found\n");
            return -1;
        }
    }
    
    return 0;
}

int jobs_builtin(t_ast_n* node, t_shell* shell, char** argv){

    if(shell->job_control_flag == -1){
        fprintf(stderr, "\nmsh: job control disabled");
        return -1;
    }

    update_no_noti_jobs(shell);

    for(size_t i = 0; i < shell->job_table_cap; i++){

        if(shell->job_table[i] == NULL) 
            continue;
        if(shell->job_table[i]->pgid == -1) 
            continue;
        if(shell->job_table[i]->pgid == shell->pgid) 
            continue;

        print_job_info(shell->job_table[i]);
    }

    return 0;
}

int fg_builtin(t_ast_n* node, t_shell* shell, char** argv){

    if(shell->job_control_flag == -1){
        fprintf(stderr, "\nmsh: job control disabled");
        return -1;
    }

    update_no_noti_jobs(shell);

    int i = 0;
    while(argv[i] != NULL) i++;
    int argc = i;

    if(argc < 2 || argc > 2){
        fprintf(stderr, "\nmsh: fg: syntax error: fg %%<JOB_ID>");
        return -1;
    }

    char* id_str = argv[1];

    if (id_str[0] == '%') {
        id_str++;
    }

    int job_id = atoi(id_str);
    if (job_id <= 0) {
        fprintf(stderr, "\nmsh: invalid job id.");
        return -1;
    }

    t_job* job = find_job(shell, job_id);
    if(!job) {
        fprintf(stderr, "\nmsh: no job with id %d", job_id);
        return -1;
    }

    if(job->position == P_FOREGROUND){
        return 0;
    }

    if(is_job_stopped(job) == 1){

        job->state = S_RUNNING;
        t_process* p = job->processes;
        while(p){
            if(!p->completed){
                p->running = 1;
                p->stopped = 0;
            }

            p = p->next;
        }
    }

    job->position = P_FOREGROUND;

    if(tcsetpgrp(shell->tty_fd, job->pgid) == -1){
        if(errno == ESRCH && job)
            del_job(shell, job->job_id, false);
        return -1;
    }

    if(kill(-job->pgid, SIGCONT) < 0){
        perror("failed to restart stopped job");
        if (tcsetpgrp(shell->tty_fd, shell->pgid) == -1) {
            perror("fg: terminal reclaim failed");
        }
        return -1;
    }
    
    wait_for_foreground_job(job, shell);

    if (tcsetpgrp(shell->tty_fd, shell->pgid) == -1) {
        perror("fg: terminal reclaim failed");
    }

    return 0;
}

int bg_builtin(t_ast_n* node, t_shell* shell, char** argv){

    if(shell->job_control_flag == -1){
        fprintf(stderr, "\nmsh: job control disabled");
        return -1;
    }

    update_no_noti_jobs(shell);

    if(!argv) 
        return -1;

    int i = 0;
    while(argv[i] != NULL) i++;
    int argc = i;

    if(argc < 2 || argc > 2){
        fprintf(stderr, "\nIncorrect usage: bg %%<JOB_ID>");
        return -1;
    }

    char* id_str = argv[1];

    if (id_str[0] == '%') {
        id_str++;
    }

    int job_id = atoi(id_str);
    if (job_id <= 0) {
        fprintf(stderr, "\nInvalid job id.");
        return -1;
    }

    t_job* job = find_job(shell, job_id);
    if(!job) {
        fprintf(stderr, "\nmsh: no job with id %d", job_id);
        return -1;
    }

    if (job->position == P_BACKGROUND && !is_job_stopped(job)) {
        fprintf(stderr, "bg: job %d is already running in background\n", job_id);
        return 0;
    }

    job->position = P_BACKGROUND;
    job->state = S_RUNNING;

    if (kill(-job->pgid, SIGCONT) < 0) {
        perror("bg: SIGCONT failed");
        return -1;
    }

    t_process* p = job->processes;
    while (p) {
        if (!p->completed) {
            p->stopped = 0;
            p->running = 1;
        }
        p = p->next;
    }

    print_job_info(job); 
    printf(" &\n"); 

    return 0;
}

int stty_builtin(t_ast_n* node, t_shell* shell, char** argv){
    
    (void)node;

    if (!isatty(shell->tty_fd)) {
        fprintf(stderr, "stty: not a tty\n");
        return -1;
    }

    struct termios t;
    if (tcgetattr(shell->tty_fd, &t) == -1) {
        perror("stty");
        return -1;
    }

    if (argv[1] == NULL) {
        if (t.c_lflag & TOSTOP)
            printf("tostop\n");
        else
            printf("-tostop\n");

        return 0;
    }

    for (int i = 1; argv[i]; i++) {

        if (strcmp(argv[i], "tostop") == 0) {
            t.c_lflag |= TOSTOP;
        }
        else if (strcmp(argv[i], "-tostop") == 0) {
            t.c_lflag &= ~TOSTOP;
        }
        else {
            fprintf(stderr, "stty: unkown option: %s\n", argv[i]);
            return -1;
        }
    }

    shell->term_ctrl.ogl_term_settings = t;

    if (tcsetattr(shell->tty_fd, TCSANOW, &shell->term_ctrl.ogl_term_settings) == -1) {
        perror("stty");
        return -1;
    }

    return 0;
}

int true_builtin(t_ast_n* node, t_shell* shell, char** argv){
    return 0;
}
int false_builtin(t_ast_n* node, t_shell* shell, char** argv){
    return -1;
}

int cond_builtin(t_ast_n* node, t_shell* shell, char** argv){

    if(strcmp(argv[4], "]") != 0){
        fprintf(stderr, "\nmsh: expected closing bracket");
        return -1;
    }

    if (strcmp(argv[2], "-lt") == 0) {
        return (atoi(argv[1]) < atoi(argv[3])) ? 0 : 1;
    } else if (strcmp(argv[2], "-gt") == 0) {
        return (atoi(argv[1]) > atoi(argv[3])) ? 0 : 1;
    } else if(strcmp(argv[2], "-eq") == 0 ){
        return (atoi(argv[1]) == atoi(argv[3])) ? 0 : 1;
    } else if(strcmp(argv[2], "-lte") == 0 ){
        return (atoi(argv[1]) <= atoi(argv[3])) ? 0 : 1;
    } else if(strcmp(argv[2], "-gte") == 0 ){
        return (atoi(argv[1]) >= atoi(argv[3])) ? 0 : 1;
    }

    return -1;
}

static void print_signals_list(void) {
    printf(" 1. SIGHUP       2. SIGINT       3. SIGQUIT      4. SIGILL\n");
    printf(" 5. SIGTRAP      6. SIGABRT      7. SIGBUS       8. SIGFPE\n");
    printf(" 9. SIGKILL     10. SIGUSR1     11. SIGSEGV     12. SIGUSR2\n");
    printf("13. SIGPIPE     14. SIGALRM     15. SIGTERM     17. SIGCHLD\n");
    printf("18. SIGCONT     19. SIGSTOP     20. SIGTSTP     21. SIGTTIN\n");
    printf("22. SIGTTOU\n");
}
int kill_builtin(t_ast_n* node, t_shell* shell, char** argv) {

    if (argv[1] == NULL) {
        fprintf(stderr, "msh: kill: not enough arguments\n");
        return -1;
    }
    
    if (strcmp(argv[1], "-l") == 0) {
        print_signals_list();
        return 0;
    }

    if (argv[2] == NULL || argv[3] != NULL) {
        fprintf(stderr, "msh: kill: incorrect syntax\n");
        return -1;
    }

    update_no_noti_jobs(shell);

    char* sig_str = (argv[1][0] == '-') ? argv[1] + 1 : argv[1];
    char* sytx_check = sig_str;
    while (*sytx_check) { 
        if (!isdigit((unsigned char)*sytx_check)) {
            fprintf(stderr, "msh: kill: invalid signal: %s\n", sig_str);
            return -1;
        }
        sytx_check++;
    }
    int signum = atoi(sig_str);

    t_job* job = NULL;
    pid_t target = -1;
    int job_id = 0;
    if (argv[2][0] == '%') {
        job = find_job(shell, atoi(argv[2] + 1));
        if (!job) {
            fprintf(stderr, "msh: kill: %s: no such job or pgid\n", argv[2]);
            return -1;
        }
        target = job->pgid;
        job_id = job->job_id;
    } else{
        target = (pid_t)atoi(argv[2]);
        job = find_job_by_pid(shell, target);
        if(job) job_id = job->job_id;
        else job_id = 0;
    }

    if (kill(-target, signum) < 0) {
        perror("msh: kill");
        return -1;
    }

    if (signum == SIGKILL || signum == SIGTERM || signum == SIGHUP || signum == SIGINT) {
        printf("[%d] Killed - %d\n", job_id, target);
        while(waitpid(-target, NULL, WNOHANG) > 0);
        if(job) del_job(shell, job->job_id, false);
        job = NULL;
    } 
    else if (signum == SIGSTOP || signum == SIGTSTP || signum == SIGTTIN || signum == SIGTTOU) {
        job->state = S_STOPPED;
        printf("[%d] Stopped - %d\n", job_id, target);
    } 
    else if (signum == SIGCONT) {
        job->state = S_RUNNING;
        printf("[%d] Resumed - %d\n", job_id, target);
    } 
    else {
        printf("[%d] Signaled (%d) - %d\n", job_id, signum, target);
    }

    update_no_noti_jobs(shell);
    return 0;
}

/**
 * @brief Builtin export command - set environment variable
 * @param node AST node containing command arguments
 * @param shell Shell context
 * @return 0 on success, 1 on failure
 */
int export_builtin(t_ast_n* node, t_shell* shell, char** argv) {

    if (argv[1] == NULL) {
        return 0;
    }

    char* str = argv[1];
    int eq_idx = -1;
    for (int i = 0; str[i]; i++) {
        if (str[i] == '=') {
            eq_idx = i;
            break;
        }
    }

    char *var_name = NULL;
    char *var_val = NULL;

    if (eq_idx == -1) {
        var_name = strdup(str);
        int local_idx = getenv_local(shell->env, var_name);
        
        if (local_idx >= 0) {
            char* entry = shell->env[local_idx];
            char* equals_ptr = strchr(entry, '=');
            var_val = strdup(equals_ptr + 1);
        } else {
            var_val = strdup("");
        }
    } else {
        var_name = strndup(str, eq_idx);
        var_val = strdup(str + eq_idx + 1);
    }

    if (!var_name || !var_val) {
        perror("msh: malloc");
        free(var_name); free(var_val);
        return -1;
    }

    if (setenv(var_name, var_val, 1) == -1) {
        perror("msh: setenv");
    }

    if (add_to_env(shell, var_name, var_val) == -1) {
        fprintf(stderr, "msh: add_to_env failed\n");
    }

    free(var_name);
    free(var_val);
    return 0;
}
/**
 * @brief Builtin unset command - remove environment variable
 * @param node AST node containing command arguments
 * @param shell Shell context
 * @return 0 on success, 1 on failure
 */
int unset_builtin(t_ast_n* node, t_shell* shell, char** argv){

    if(argv[1] == NULL){
        fprintf(stderr, "\nmsh: usage: unset <VAR>");
        return -1;
    } else if(unsetenv(argv[1]) == -1){
        perror("\nunsetenv fail");
        return -1;
    }

    if(remove_from_env(shell, argv[1]) == -1){
        fprintf(stderr, "\nmsh: var not found %s", argv[1]);
        return -1;
    }

    return 0;
}

int set_builtin(t_ast_n* node, t_shell* shell, char** argv) {

    if (argv[1] == NULL) {
        fprintf(stderr, "msh: usage: set <VAR>=<VAL>\n");
        return -1;
    }

    char* str = argv[1];
    int eq_idx = -1;
    for (int i = 0; str[i]; i++) {
        if (str[i] == '=') {
            eq_idx = i;
            break;
        }
    }

    if (eq_idx == -1) {
        fprintf(stderr, "msh: set: %s: invalid assignment\n", str);
        return -1;
    }

    char* var_name = strndup(str, eq_idx);
    char* var_val = strdup(str + eq_idx + 1);

    if (!var_name || !var_val) {
        perror("msh: malloc");
        if(var_name)
            free(var_name);
        if(var_val)
            free(var_val);
        return -1;
    }

    if (add_to_env(shell, var_name, var_val) == -1) {
        fprintf(stderr, "msh: failed to set variable %s\n", var_name);
        free(var_name);
        free(var_val);
        return -1;
    }

    free(var_name);
    free(var_val);

    return 0;
}

/**
 * @brief Builtin clear command - clear terminal screen
 * @param node AST node containing command arguments
 * @param shell Shell context
 * @return Always returns 0
 */
int clear_builtin(t_ast_n* node, t_shell* shell, char** argv){
    
    printf("\033[2J\033[H");

    return 0;
}

/**
 * @brief Builtin alias command - manage command aliases
 * @param node AST node containing command arguments
 * @param shell Shell context
 * @return 0 on success, -1 on failure
 */
int alias_builtin(t_ast_n* node, t_shell* shell, char** argv){

    if(argv[1] == NULL){
        print_alias_ht(&(shell->aliases));
        return 0;
    } else if(argv[2] != NULL){
        fprintf(stderr, "\nmsh: bad assignment");
    }

    int eq_idx = -1;
    char* str = argv[1];
    for(int i = 0; str[i]; i++){
        if(str[i] == '=') {
            eq_idx = i;
            break;
        }
    }
    if(eq_idx == -1) 
        return -1;

    char* alias = strndup(argv[1], eq_idx);
    size_t alias_len = strlen(argv[1]) - eq_idx - 1;
    char* aliased_cmd = strndup(argv[1] + eq_idx + 1, alias_len);
    t_alias_ht_node* n = insert_alias(&(shell->aliases), alias, aliased_cmd);
    free(aliased_cmd);
    free(alias);
    if(!n){
        perror("insert alias");
        return -1;
    }

    return 0;
}

/**
 * @brief Builtin unalias command - remove command aliases
 * @param node AST node containing command arguments
 * @param shell Shell context
 * @return Always returns 0
 */
int unalias_builtin(t_ast_n* node, t_shell* shell, char** argv){

    if(argv[1] == NULL){
        printf("\nUnalias what?");
        return 0;
    }
    if(strcmp(argv[1], "all") == 0){
        flush_alias_ht(&(shell->aliases));
        printf("\nFlushed aliases.");
        return 0;
    }
    if(hash_delete_alias(&(shell->aliases), argv[1]) == -1){
        printf("\nUnalias call failed: Alias not found.");
    }

    return 0;
}

/**
 * @brief Builtin exit command - terminate shell
 * @param node AST node containing command arguments
 * @param shell Shell context
 * @return Never returns (calls exit())
 */
int exit_builtin(t_ast_n* node, t_shell* shell, char** argv){
    
    int exit_status = 0;
    if(argv && argv[1] != NULL){
        exit_status = atoi(argv[1]);
    }

    if(shell->job_table != NULL){

        for(size_t i = 0; i < shell->job_table_cap; i++){
            t_job* job = shell->job_table[i];
            if(!job) continue;

            kill(-job->pgid, SIGKILL);
        }
    }

    cleanup_argv(argv);
    exit(exit_status);

    return 0; ///< Suppress err (unreachable)
}

/**
 * @brief Builtin env command - print environment variables
 * @param node AST node containing command arguments
 * @param shell Shell context
 * @return Always returns 0
 */
int env_builtin(t_ast_n* node, t_shell* shell, char** argv){
    
    char** env = shell->env;
    int i = 0;
    while(env[i] != NULL){
        printf("\n%s", env[i]);
        i++;
    }

    return 0;
}

/**
 * @brief Builtin history command - print command history
 * @param node AST node containing command arguments
 * @param shell Shell context
 * @return Always returns 0
 */
int history_builtin(t_ast_n* node, t_shell* shell, char** argv){

    if(shell->history.size == 0){
        return 0;
    }

    int max = shell->history.size;

    if(argv[1] != NULL){
        max = atoi(argv[1]);
        if(max == 0)
            max = shell->history.size;
    }

    t_dllnode* ptr = shell->history.head->next;
    int i = 0;
    while(ptr && i < max){
        printf("\n%d      %s", i + 1, ptr->strbg);
        ptr = ptr->next;
        i++;
    }

    return 0;
}