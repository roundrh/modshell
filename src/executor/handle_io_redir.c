#include"handle_io_redir.h"

/**
 * @file handle_io_redir.c
 * @brief implementation of handling of I/O redirection
 */

/**
 * @brief helper function to get redirection flags based on redirection type.
 * @return flags
 * @param node pointer to ast node
 * @param index index of io_redir arr
 */
static int get_redir_flags(t_ast_n* node, int index){

    int flags = 0;
    
    if (node->io_redir[index]->io_redir_type == IO_TRUNC) {
        flags = O_WRONLY | O_CREAT | O_TRUNC;
    } else if(node->io_redir[index]->io_redir_type == IO_APPEND){
        flags = O_WRONLY | O_CREAT | O_APPEND;
    }else{
        flags = O_RDONLY;
    }

    return flags;
}

/**
 * @brief handles heredoc_io
 * @return 0 success, -1 fail (fatal)
 * @param shell pointer to shell struct
 * @param index index of io_redir arr
 * @param node pointer to ast node
 *
 * Function first calls mkstemp(template) on defined char template[] to create a temp file to read heredoc i/o into
 * fcntl and unlink assure the deletion of the flag after heredoc is read.
 * reading of user input for heredoc handled within this function.
 */
static int heredoc_io(t_shell* shell, int index, t_ast_n* node){

    char template[] = "/tmp/heredocXXXXXX";
    int heredoc_fd = mkstemp(template);
    if(heredoc_fd == -1){
        perror("mkstemp fatal error");
        return -1;
    }


    fcntl(heredoc_fd, F_SETFD, FD_CLOEXEC);

    unlink(template);
    char* delim = node->io_redir[index]->filename; // cmd << DELIM
    char* buf = NULL;
    size_t buf_len = 0;
    ssize_t char_read = 0;


    while(1){

        printf("HEREDOC>> ");
        fflush(stdout);  

        if((char_read = getline(&buf, &buf_len, stdin)) == -1){
            break;
        }

        if (char_read > 0 && buf[char_read - 1] == '\n') {
            buf[char_read - 1] = '\0';
        }

        if(strcmp(buf, delim) == 0){
            break;
        }  

        if(write(heredoc_fd, buf, strlen(buf)) == -1){
            perror("fatal inloop err heredoc");
            break;
        }
        if(write(heredoc_fd, "\n", 1) == -1){
            perror("fatal inloop err heredoc");
            break;
        }
    }

    free(buf);
    buf = NULL;

    if (lseek(heredoc_fd, 0, SEEK_SET) == -1) {
        perror("lseek fatal error");
        close(heredoc_fd);
        return -1;
    }

    return heredoc_fd;
}

/**
 * @brief applies a single redirection at index of io_redir array in node
 * @return 0 success, -1 fail (fatal)
 * @param shell pointer to shell struct
 * @param node pointer to ast node
 * @param index index of io_redir arr
 *
 */
static int apply_single_redir(t_shell* shell, t_ast_n* node, int index){

    if(node->io_redir[index]->filename == NULL){
        fprintf(stderr, "\nInvalid syntax.");
        return -1;
    }

    int newfd = -1;
    int flags = get_redir_flags(node, index);

    if(node->io_redir[index]->io_redir_type == IO_HEREDOC){
        newfd = heredoc_io(shell, index, node);
    } else{
        newfd = open(node->io_redir[index]->filename, flags, 0644);
        if(newfd == -1){
            perror("fatal open err");
            return -1;
        }
    }

    if(node->io_redir[index]->io_redir_type == IO_TRUNC || node->io_redir[index]->io_redir_type == IO_APPEND){
        dup2(newfd, STDOUT_FILENO);
    } else{
        dup2(newfd, STDIN_FILENO);
    }

    close(newfd);
    return 0;
}

/**
 * @brief calls apply_single_redir iteratively on all indexes within io_redir array of node
 * @return 0 success, -1 fail (fatal)
 * @param shell pointer to shell struct
 * @param node pointer to ast node
 *
 */
int redirect_io(t_shell* shell, t_ast_n* node) {

    if(node->io_redir == NULL){
        return -1;
    }

    if (shell->std_fd_backup[0] == -1) {  
        shell->std_fd_backup[0] = dup(STDIN_FILENO);
        shell->std_fd_backup[1] = dup(STDOUT_FILENO);
    }
    
    for (int i = 0; node->io_redir[i] != NULL; i++) {
        
        if (apply_single_redir(shell, node, i) == -1) {
            restore_io(shell);
            return -1;
        }
    }
    return 0;
}

/**
 * @brief restores I/O file descriptors backed up by restore_io into shell struct.
 * @return 0 success, -1 fail (fatal)
 * @param shell pointer to shell struct
 *
 */
int restore_io(t_shell* shell) {

    if (shell->std_fd_backup[0] != -1) {

        dup2(shell->std_fd_backup[0], STDIN_FILENO);
        dup2(shell->std_fd_backup[1], STDOUT_FILENO);

        close(shell->std_fd_backup[0]);
        close(shell->std_fd_backup[1]);

        shell->std_fd_backup[0] = -1;
        shell->std_fd_backup[1] = -1;
    }
    return 0;
}