#include"executor.h"

/**
 * @file executor.c
 * @brief implementation of functions used to execute AST.
 */

static t_shell* g_shell_ptr = NULL;
void set_global_shell_ptr_chld(t_shell* ptr){
    g_shell_ptr = ptr;
}

void cleanup_global_cmd_buf_ptr(void);  ///< defined in shell_driver.c

static pid_t exec_pipe(t_ast_n* node, t_shell* shell, t_job* job, int subshell);            ///< Forward declaration of function    
static pid_t exec_command(t_ast_n* node, t_shell* shell, t_job* job, int is_pipeline_child, int subshell, t_ast_n* pipeline);         ///< Forward declaration of function
static pid_t exec_simple_command(t_ast_n* node, t_shell* shell, t_job* job, int is_pipeline_child, int subshell, t_ast_n* pipeline);  ///< Forward declaration of function
static int exec_list(char* cmd_buf, t_ast_n* node, t_shell* shell, int subshell, t_job* subshell_job);
static inline int handle_tcsetpgrp(t_shell* shell, pid_t pgid);

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

static void cleanup_flattened_ast(t_ast_n* node){
    
    if(!node)
        return;

    t_ast_n* p = node;
    while(p){
        t_ast_n* next = p->right;

        free(p);
        p = next;
    }
}   

static t_wait_status wait_for_foreground_job(t_job* job, t_shell* shell) {

    pid_t pid = 0;
    int status = 0;

    while(!is_job_completed(job) && !is_job_stopped(job)){

        pid = waitpid(-job->pgid, &status, WUNTRACED);
        if(pid == -1){
            if(errno == ECHILD){
                break;
            } else if(errno == EINTR){
                continue; ///< Ignore whatever signal stopped it.
            } 
            else{
                perror("waitpid");
                return WAIT_ERROR;
            }
        }

        t_process* process = find_process_in_job(job, pid);
        if(!process) continue;

        if(WIFEXITED(status)){

            process->completed = 1;
            process->stopped = 0;
            process->running = 0;

            process->exit_status = WEXITSTATUS(status);
            job->last_exit_status = WEXITSTATUS(status);

        } else if(WIFSTOPPED(status)){

            process->stopped = 1;
            process->completed = 0;
            process->running = 0;
            job->state = S_STOPPED;
            job->position = P_BACKGROUND;

            job->last_exit_status = WEXITSTATUS(status);
            shell->last_exit_status = job->last_exit_status;
            return WAIT_STOPPED;

        } else if (WIFSIGNALED(status)){

            process->completed = 1;
            process->stopped = 0;
            process->running = 0;
            
            int sig = WTERMSIG(status);

            process->exit_status = 128 + sig;
            job->last_exit_status = 128 + sig;
        }

    }

    shell->last_exit_status = job->last_exit_status;
    return WAIT_FINISHED;
}

static t_job* make_job(t_shell* shell, char* buf, t_state state, pid_t pgid, t_position pos){

    t_job* job = NULL;

    job = (t_job*)malloc(sizeof(t_job));
    if(job == NULL){
        perror("job malloc makejob");
        return NULL;
    }

    init_job_struct(job);

    if(buf != NULL){
        job->command = strdup(buf);
        if(job->command == NULL){
            cleanup_job_struct(job);
            return NULL;
        }
    } else{
        job->command = NULL;
    }

    job->position = pos;
    job->pgid = pgid;
    job->state = state;

    if(add_job(shell, job) == -1){
        cleanup_job_struct(job);
        free(job);
        return NULL;
    }

    return job;
}

static t_process* make_process(pid_t pid){

    t_process* process = (t_process*)malloc(sizeof(t_process));
    if(process == NULL){
        perror("makeprocess malloc fail");
        return NULL;
    }

    process->pid = pid;
    process->exit_status = -1;
    process->completed = 0;
    process->stopped = 0;
    process->running = 0;

    process->next = NULL;

    return process;
}

static pid_t exec_extern_cmd(t_shell* shell, t_ast_n* node, t_job* job, int is_pipeline_child,  int subshell, t_ast_n* pipeline, char** argv){

    if(!argv){
        perror("argv prop err");
        return -1;
    }

    if(is_pipeline_child){
        execvp(argv[0], argv);
        fprintf(stderr, "msh: command \"%s\" not found\n", argv[0]);
        _exit(127);
    }

    pid_t pid = fork();
    if(pid == -1){
        perror("fork fail exec_extern");
        return -1;
    }  //fork error.

    if(job->pgid == -1)
        job->pgid = pid;

    if(pid == 0){

        if(!subshell){
            if(setpgid(0, job->pgid) < 0){
                if(errno != EPERM && errno != EACCES && errno != ESRCH){
                    perror("280: setpgid");
                    _exit(EXIT_FAILURE);
                }
            }
        }

        init_ch_sigtable(&(shell->shell_sigtable));
        set_global_shell_ptr_chld(shell);

        execvp(argv[0], argv);
        fprintf(stderr, "msh: command \"%s\" not found\n", argv[0]);
        _exit(127);

    } else if(shell->job_control_flag){

        if(setpgid(pid, job->pgid) == -1){
            if(errno != EPERM && errno != EACCES && errno != ESRCH){
                perror("230: parent setpgid");
                return -1;
            }
        }
        t_process* process = make_process(pid);
        if(!process) 
            return -1;
        if(add_process_to_job(job, process) == -1){
            perror("fail to add process to job");
            return -1;
        }   
    }

    return pid;
}
/**
 * @brief executes simple command in node
 * @param node pointer to ast node
 * @param shell pointer to shell struct
 * @return -1 on fail, 0 on success.
 */
static pid_t exec_simple_command(t_ast_n* node, t_shell* shell, t_job* job, int is_pipeline_child,  int is_subshell, t_ast_n* pipeline){

    if(!node)
        return -1;

    char** argv = NULL;
    t_err_type err_ret = expand_make_argv(shell, &argv, node->tok_start, node->tok_segment_len);
    if(err_ret == err_fatal){
        perror("fatal err expanding argv");
        exit_builtin(node, shell, NULL);
    } else if(argv == NULL || argv[0] == NULL){
        cleanup_argv(argv);
        return 0;
    }

    t_ht_node* builtin_imp =  hash_find_builtin(&shell->builtins, argv[0]);
    if(builtin_imp == NULL){
        pid_t ret_pid = exec_extern_cmd(shell, node, job, is_pipeline_child, is_subshell, pipeline, argv);
        cleanup_argv(argv);
        return ret_pid;
    }

    job->last_exit_status = builtin_imp->builtin_ptr(node, shell, argv);
    cleanup_argv(argv);
    shell->last_exit_status = job->last_exit_status;

    /* pid 0 on built in execution -- denotes no fork -- shell last exit status set */
    return 0;
}

static pid_t exec_subshell(t_ast_n* node, t_shell* shell, t_job* job, int is_pipeline_child, t_ast_n* pipeline, int subshell){
    
    pid_t pid = fork();
    if(pid < 0){
        perror("241: fork fail");
        return -1;
    }

    if(!subshell && job->pgid == -1)
        job->pgid = pid;

    if(pid == 0){

        if(!subshell){
            if(setpgid(0, job->pgid) < 0){
                if(errno != EPERM && errno != EACCES && errno != ESRCH){
                    perror("379: setpgid");
                    _exit(EXIT_FAILURE);
                }
            }
        }

        shell->job_control_flag = 0;
        init_ch_sigtable(&(shell->shell_sigtable));
        set_global_shell_ptr_chld(shell);

        int exit_status = exec_list(NULL, node->sub_ast_root, shell, 1, job);

        int status = 0;
        while(waitpid(-job->pgid, &status, 0) > 0);

        _exit(exit_status);
    } else if(shell->job_control_flag) {

        if(setpgid(pid, job->pgid) < 0){
            if(errno != EPERM && errno != EACCES && errno != ESRCH)
                perror("setpgid");
        }

        t_process* process = make_process(pid);
        if(!process) return -1;
        if(add_process_to_job(job, process) == -1){
            perror("fail to add process to jod");
            return -1;
        }
    }

    return pid;
}

/**
 * @brief executes command in node based on saved OP_TYPE by parser
 * @param node pointer to ast node
 * @param shell pointer to shell struct
 * @return -1 on fail, 0 on success.
 *
 */
static pid_t exec_command(t_ast_n* node, t_shell* shell, t_job* job, int is_pipeline_child, int subshell, t_ast_n* pipeline){

    if(!node)
        return -1;

    int restore_io_flag = 0;
    if(node->redir_bool){
        if(redirect_io(shell, node) == -1){
            fprintf(stderr, "\nErr redir io");
            return -1;
        }
        node->redir_bool = 0;
        restore_io_flag = 1;
    }

    pid_t pid = -1;
    if(node->op_type == OP_PIPE){
        pid = exec_pipe(node, shell, job, subshell);
    } else if(node->op_type == OP_SIMPLE){
        pid = exec_simple_command(node, shell, job, is_pipeline_child, subshell, pipeline);
    } else if(node->op_type == OP_SUBSHELL){
        pid = exec_subshell(node, shell, job, is_pipeline_child, pipeline, subshell);
    }

    if(restore_io_flag)  
        restore_io(shell);

    return pid;
}

static t_ast_n* flatten_ast(t_ast_n* node) {

    if (!node)
        return NULL;

    if (node->op_type == OP_PIPE) {
        t_ast_n* left_list = flatten_ast(node->left);
        t_ast_n* right_list = flatten_ast(node->right);

        t_ast_n* tail = left_list;
        if (tail) {
            while (tail->right) {
                tail = tail->right;
            }

            tail->right = right_list;

            return left_list;
        } else {
            return right_list;
        }
    } else {
        t_ast_n* new_node = (t_ast_n*)malloc(sizeof(t_ast_n));
        if (!new_node)
            return NULL;

        init_ast_node(new_node);

        new_node->tok_start = node->tok_start;
        new_node->tok_segment_len = node->tok_segment_len;
        new_node->background = node->background;
        new_node->op_type = node->op_type;
        new_node->sub_ast_root = node->sub_ast_root;
        new_node->io_redir = node->io_redir;

        new_node->redir_bool = node->redir_bool;

        new_node->left = NULL;
        new_node->right = NULL;

        return new_node;
    }
}
/**
 * @brief executes pipe command on flattened list
 * @param node pointer to ast node
 * @param shell pointer to shell struct
 * @return -1 on fail, 0 on success.
 *
 * @note double fork caused bad race condition, fixed
 * @note 428 pipes fails with EMFILE: too many open files errno 
 *     cleans up flattened ast returns -1 propagates back for conditional commands.
 * @note _exit(shell->last_exit_status) in children is irrelevant
 * each child forks and the parent reaps the final exit status of the exec'd command 
 *  for builtins in pipes that do reach _exit() and have set shell->last_exit_status for the forked child to _exit with
 */
static pid_t exec_pipe(t_ast_n* node, t_shell* shell, t_job* job, int subshell){

    pid_t last_pid = -1;

    t_ast_n* pipeline = flatten_ast(node);
    if(!pipeline){
        perror("fail to flatten");
        
        /* fail to malloc fatal*/
        exit_builtin(node, shell, NULL);
    }

    t_ast_n* head = pipeline;
    int count_cmd = 0;
    while(head){
        count_cmd++;
        head = head->right;
    }

    if(count_cmd < 1) return -1;

    int (*pipes)[2] = malloc(sizeof(int[2]) * (count_cmd - 1));
    for(int i = 0; i < count_cmd - 1; i++){
        pipes[i][0] = -1;
        pipes[i][1] = -1;
    }

    for(int i = 0; i < count_cmd - 1; i++){
        if(pipe(pipes[i]) == -1){
            for(int j = 0; j < i; j++){
                close(pipes[j][0]);
                close(pipes[j][1]);
            }
            cleanup_flattened_ast(pipeline);
            pipeline = NULL;
            perror("pipe fail");
            return -1;
        }
    }


    t_ast_n* exec = pipeline;
    int i = 0;
    while(exec && i < count_cmd) {

        pid_t pid = fork();
        if(pid == -1){
            return -1;
        }

        if(job->pgid == -1 && i == 0){
            job->pgid = pid;
        }

        if(pid == 0){

            if(i==0){

                if(!subshell){
                    if(setpgid(0, job->pgid) < 0){
                        if(errno != EPERM && errno != EACCES && errno != ESRCH){
                            perror("280: setpgid");
                            _exit(EXIT_FAILURE);
                        }
                    }
                }

                if(dup2(pipes[i][1], STDOUT_FILENO) == -1){
                    perror("dup2");
                }

                for (int j = 0; j < count_cmd - 1; j++) {
                    close(pipes[j][0]);
                    close(pipes[j][1]);
                }

                init_ch_sigtable(&shell->shell_sigtable);
                set_global_shell_ptr_chld(shell);

                exec_command(exec, shell, job, 1, subshell, pipeline);

                _exit(shell->last_exit_status);
            } else if(i == count_cmd - 1){

                if(!subshell){
                    if(setpgid(0, job->pgid) < 0){
                        if(errno != EPERM && errno != EACCES && errno != ESRCH){
                            perror("280: setpgid");
                            _exit(EXIT_FAILURE);
                        }
                    }
                }

                dup2(pipes[i-1][0], STDIN_FILENO);
                for (int j = 0; j < count_cmd-1; j++) {
                    close(pipes[j][0]);
                    close(pipes[j][1]);
                }

                init_ch_sigtable(&shell->shell_sigtable);
                set_global_shell_ptr_chld(shell);
                
                exec_command(exec, shell, job, 1, subshell, pipeline);

                _exit(shell->last_exit_status);
            } else{

                if(!subshell){
                    if(setpgid(0, job->pgid) < 0){
                        if(errno != EPERM && errno != EACCES && errno != ESRCH){
                            perror("280: setpgid");
                            _exit(EXIT_FAILURE);
                        }
                    }
                }

                dup2(pipes[i-1][0], STDIN_FILENO);
                dup2(pipes[i][1], STDOUT_FILENO);
                for (int j = 0; j < count_cmd-1; j++) {
                    close(pipes[j][0]);
                    close(pipes[j][1]);
                }

                init_ch_sigtable(&shell->shell_sigtable);
                set_global_shell_ptr_chld(shell);

                exec_command(exec, shell, job, 1, subshell, pipeline);

                _exit(shell->last_exit_status);
            }
        } else if(shell->job_control_flag) {

            if(setpgid(pid, job->pgid) < 0){
                if(errno != EPERM && errno != EACCES && errno != ESRCH){
                    perror("setpgid fail");
                    return -1;
                }
            }

            t_process* process = make_process(pid);
            if(!process){
                perror("process make fail");
                cleanup_job_struct(job);
                return -1;
            }

            add_process_to_job(job, process);
        }

        last_pid = pid;

        exec = exec->right;
        i++;
    }

    if(pipeline){
        cleanup_flattened_ast(pipeline);
        pipeline = NULL;
    }

    for(int j = 0; j < count_cmd - 1; j++){
        close(pipes[j][0]);
        close(pipes[j][1]);
    }

    free(pipes);

    return last_pid;
}

static inline int handle_tcsetpgrp(t_shell* shell, pid_t pgid){

    while(tcsetpgrp(shell->tty_fd, pgid) == -1){
        if(errno == EINTR)
            continue;
        else if(errno == EPERM || errno == EINVAL)
            break;
        else{
            perror("630: tcsetpgrp");
            return -1;
        }
    }

    return 0;
}

static int exec_job(char* cmd_buf, t_ast_n* node, t_shell* shell, int subshell, t_job* subshell_job){

    t_job* job = NULL;
    if(!subshell)
        job = make_job(shell, cmd_buf, S_RUNNING, -1, node->background ? P_BACKGROUND : P_FOREGROUND);
    else
        job = subshell_job;
    if(!job){
        return -1;
    }

    exec_command(node, shell, job, 0, subshell, NULL);

    if(shell->job_control_flag && job->position == P_FOREGROUND){

        if(handle_tcsetpgrp(shell, job->pgid) == -1){
            perror("713: tcsetpgrp yield");
            exit_builtin(node, shell, NULL);
        }

        t_wait_status job_status = wait_for_foreground_job(job, shell);

        if(handle_tcsetpgrp(shell, shell->pgid) == -1){
            perror("720: tcsetpgrp reclaim");
            exit_builtin(node, shell, NULL);
        }

        if(job_status == WAIT_FINISHED)
            del_job(shell, job->job_id);

        else if(job_status == WAIT_STOPPED){
            print_job_info(job);
            shell->last_exit_status = 0;
        } else{
            perror("687: failed wait for fg fatal");
            return -1;
        }

    } else if(shell->job_control_flag) {
        if(!subshell)
            print_job_info(job);
        shell->last_exit_status = 0;
    } else if(!shell->job_control_flag && job->position == P_FOREGROUND){
        int status = 0;
        pid_t pid;
        while((pid = waitpid(-job->pgid, &status, 0)) > 0){
            if(pid > 0){
                shell->last_exit_status = WEXITSTATUS(status);
            }
        }
    }

    return shell->last_exit_status;
}

static int exec_list(char* cmd_buf, t_ast_n* node, t_shell* shell, int subshell, t_job* subshell_job){

    if(!node)
        return 0;

    switch (node->op_type){

        case OP_SEQ:
            exec_list(cmd_buf, node->left, shell, subshell, subshell_job);
            exec_list(cmd_buf, node->right, shell, subshell, subshell_job);
            return shell->last_exit_status;
        case OP_AND:
            exec_list(cmd_buf, node->left, shell, subshell, subshell_job);
            if(shell->last_exit_status == 0)
                exec_list(cmd_buf, node->right, shell, subshell, subshell_job);
            return shell->last_exit_status;
        case OP_OR:
            exec_list(cmd_buf, node->left, shell, subshell, subshell_job);
            if(shell->last_exit_status != 0)
                exec_list(cmd_buf, node->right, shell, subshell, subshell_job);
            return shell->last_exit_status;
        default:
            return exec_job(cmd_buf, node, shell, subshell, subshell_job);
    }

    return 0;
}
/**
 * @brief called by driver to build ast and execute the ast via recursive descent.
 * @param cmd_buf pointer to cmd line buf
 * @param shell pointer to shell struct
 * @param command pointer to command struct
 * @return -1 on fail, 0 on success.
 *
 */
int parse_and_execute(char** cmd_buf, t_shell* shell, t_token_stream* token_stream){

    t_alias_hashtable* aliases = &(shell->aliases);
    if(lex_command_line(cmd_buf, token_stream, aliases, 0) == -1){
        return -1;
    }

    t_ast_n* root;
    if((root = build_ast(&(shell->ast), token_stream)) == NULL){
        perror("fatal building ast");
        return -1;
    }

    sigset_t block_mask, old_mask;
    int exec_flag = 1;
    if(sigemptyset(&block_mask) == -1){
        perror("sigemptyset");
        exec_flag = 0;
    }
    if(sigaddset(&block_mask, SIGCHLD)){
        perror("sigaddset");
        exec_flag = 0;  
    }

    if(sigprocmask(SIG_BLOCK, &block_mask, &old_mask) == -1){
        perror("sigproc");
        exec_flag = 0;
    }

    if(exec_flag){
        exec_list(*cmd_buf, root, shell, 0, NULL);
    }
    
    if(sigprocmask(SIG_SETMASK, &old_mask, NULL) == -1){
        perror("sigproc");
    }

    cleanup_ast(root);
    restore_io(shell);
    shell->ast.root = NULL;
    
    return 0;
}
