#include "userinp.h"

/**
 * @file userinp.c
 * @brief contains implementation of functions to read user input
 */

/**
 * @brief handle read error
 * @return an int to read_status to know how to handle read fail
 * @param ssize_t bytes_read from functions
 * @param buf from functions
 *
 * If errno returns EINTR, EAGAIN, or EWOULDBLOCK, input loop continues
 * Else input loop returns -1, fatal error.
 */
static int handle_read_error(char *buf, ssize_t bytes_read){
    if (bytes_read == 0) {
        return 1;
    } // EOF
    
    /*bytes_read < 0 sets errno to one of these*/
    if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
        return 0;
    } else {
        perror("readinp: fatal read error");
        errno = EIO;
        return -1;
    }// !FATAL
}

int handle_write_fail(int fd, const char *buf, size_t len, char* buffer_ptr){

    ssize_t write_ret;

    while ((write_ret = write(fd, buf, len)) == -1) {
        if (errno == EINTR)
            continue;

        if (errno == EPIPE)
            return -1;

        perror("readinp fail: write fatal");
        if(buffer_ptr){
            free(buffer_ptr);
        }
        return -1;
    }
    return 0;
}

/**
 * @brief handle reallocation of line buffer
 * @return -1 on fail 0 on success.
 * @param buf pointer to array of characters line buffer
 * @param buf_cap capacity of line buffer
 * @param buf_len length of line buffer
 *
 * function is called in the loop to check if the buffer needs to be reallocated
 * to handle a large capacity of input.
*/
static int handle_realloc_buf(char** buf, size_t* buf_cap, size_t* buf_len){

    if(!buf || !(*buf) || !buf_cap || !buf_len || *buf_cap <= 0)
        return 0; //bad pass

    if(*buf_len < *buf_cap - 1)
        return 0; //no realloc needed

    size_t new_size = (*buf_cap) * BUF_GROWTH_FACTOR;
    if(new_size >= MAX_COMMAND_LENGTH){
        fprintf(stderr, "\ncmd max length reached");
        free(*buf);
        *buf = NULL;
        return -1;
    }

    char* new_buf = realloc(*buf, new_size);
    if(!new_buf){
        perror("fail to realloc new_buf");
        free(*buf);
        *buf = NULL;
        return -1;
    }


    *buf = new_buf;
    *buf_cap = new_size;
    new_buf = NULL;

    return 1;
}

/**
 * @brief force reallocation of line buffer
 * @return -1 on fail 0 on success.
 *
 * @param buf pointer to array of characters line buffer
 * @param buf_cap capacity of line buffer
 * @param new_buf_cap new capacity of line buffer
 *
 * function is called to force the reallocation of the line buffer to a new buffer capacity.
*/
static int force_realloc_buf(char** buf, size_t* buf_cap, size_t* new_buf_cap){

    if(!buf || !(*buf) || !buf_cap || !new_buf_cap || *new_buf_cap == 0 || *buf_cap == 0)
        return 0; //bad pass

    if(*new_buf_cap >= MAX_COMMAND_LENGTH){
        fprintf(stderr, "how did you even get here");
        free(*buf);
        *buf = NULL;
        return -1;
    }
    
    char* new_buf = realloc(*buf, *new_buf_cap);
    if(!new_buf){
        perror("bad realloc");
        free(*buf);
        *buf = NULL;
        return -1;
    }

    *buf = new_buf;
    *buf_cap = *new_buf_cap;

    return 1;
}

/**
 * @brief switches termios to VMIN 0, VTIME 1
 * @return -1 on fail 0 on success.
 *
 * @param shell pointer to shell struct
 *
 * Switches VMIN to 0 and VTIME to 1 to allow for 3 byte sequence to arrive for arrow key parsing.
 *
 * @note function used to read arrow key input.
*/
static int temp_switch_termios(t_shell* shell){

    struct termios temptio = shell->term_ctrl.new_term_settings;

    temptio.c_cc[VMIN] = 0;
    temptio.c_cc[VTIME] = 1;

    return tcsetattr(STDIN_FILENO, TCSANOW, &temptio);
}

static t_dllnode* search_history(t_shell* shell, char* cmd){

    size_t cmd_len = strlen(cmd);
    if(cmd_len == 0) 
        return NULL;

    t_dllnode* h = shell->history.head;
    while(h){
        if(strncmp(h->strbg, cmd, strlen(cmd)) == 0)
            return h;
        h = h->next;
    }
    return NULL;
}

static void render_suggestion(char *cmd, size_t cmd_len, t_dllnode *suggestion_node) {

    if(cmd_len == 0 || !suggestion_node) {
        HANDLE_WRITE_FAIL_FATAL(STDIN_FILENO, "\033[J", 3, NULL);
        return;
    }

    HANDLE_WRITE_FAIL_FATAL(STDIN_FILENO, "\033[s", 3, NULL);
    HANDLE_WRITE_FAIL_FATAL(STDIN_FILENO, "\033[J", 3, NULL);

    char *suggestion = suggestion_node->strbg + cmd_len;
    HANDLE_WRITE_FAIL_FATAL(STDIN_FILENO, "\033[38;5;8m", 10, NULL);
    HANDLE_WRITE_FAIL_FATAL(STDIN_FILENO, suggestion, strlen(suggestion), NULL);
    HANDLE_WRITE_FAIL_FATAL(STDIN_FILENO, "\033[0m", 4, NULL);

    HANDLE_WRITE_FAIL_FATAL(STDIN_FILENO, "\033[u", 3, NULL);
}

/**
 * @brief reads user input into cmd
 * @return pointer to array of char, NULL on fail.
 *
 * @param shell pointer to shell struct
 *
 * Function handles backspace logic, arrow keys for history switching, and shifting of characters as needed
 * with proper indexing of charactrs.
 */
char* read_user_inp(t_shell* shell){

    t_dllnode* ptr = shell->history.head;
    t_dllnode* suggestion_node = NULL;

    size_t cmd_cap = INITIAL_COMMAND_LENGTH;
    char* cmd = (char*)malloc(sizeof(char) * INITIAL_COMMAND_LENGTH);
    if(!cmd){
        perror("malloc fail: buf read user inp");
        return NULL;
    }
    cmd[0] = '\0';

    size_t cmd_len = 0;
    int cmd_idx = 0;
    
    while(1){

        if(handle_realloc_buf(&cmd, &cmd_cap, &cmd_len) == -1)
            return NULL; //cmd freed in handle_realloc_buf

        char c = '\0';

        ssize_t bytes_read = read(STDIN_FILENO, &c, 1);
        int read_status = 0;

        if(bytes_read <= 0){
            read_status = handle_read_error(cmd, bytes_read);
            if(read_status == 0){
                continue;
            } else if(read_status == 1){
                break;
            } else if(read_status == -1){
                perror("read: userinp");
                free(cmd);
                return NULL;
            }
        }

        if (cmd_idx != cmd_len) {
            suggestion_node = NULL;
        }

        //  '\x1b'
        if(c == 27){

            temp_switch_termios(shell);

            char seq_end[3] = {0};

            ssize_t seq_bytes = read(STDIN_FILENO, seq_end, 3);
            int read_status_seq = 0;

            if(seq_bytes <= 0){
                read_status_seq = handle_read_error(cmd, bytes_read);
                if(read_status_seq == 0){
                    continue;
                } else if(read_status_seq == 1){
                    break;
                } else if(read_status_seq == -1){
                    perror("read: userinp");
                    free(cmd);
                    return NULL;
                }
            }

            if(tcsetattr(STDIN_FILENO, TCSANOW, &(shell->term_ctrl.new_term_settings)) == -1){
                perror("fail to reset terminal");
                free(cmd);
                cmd = NULL;
                return cmd;
            }

            if (seq_bytes > 0 && seq_end[0] == '[' && seq_end[1] == 'C') {
                if (cmd_idx == cmd_len && suggestion_node) {
                    size_t sugg_len = strlen(suggestion_node->strbg);
                    if (sugg_len >= cmd_cap) force_realloc_buf(&cmd, &cmd_cap, &sugg_len);
                    
                    char *to_add = suggestion_node->strbg + cmd_idx;
                    HANDLE_WRITE_FAIL_FATAL(STDIN_FILENO, to_add, strlen(to_add), cmd);
                    
                    strcpy(cmd, suggestion_node->strbg);
                    cmd_idx = cmd_len = sugg_len;
                    suggestion_node = NULL;
                    continue; 
                }
            }
            
            if(seq_bytes > 0 && seq_end[0] == '['){
                if(seq_end[1] == 'A'){

                    int i = cmd_idx;
                    while(cmd[i] != '\0'){
                        HANDLE_WRITE_FAIL_FATAL(STDIN_FILENO, "\033[C", 3, cmd);
                        i++;
                    }
                    for(int k = 0; k < cmd_len; k++)
                        HANDLE_WRITE_FAIL_FATAL(STDIN_FILENO, "\b \b", 3, cmd);

                    cmd_idx = cmd_len = 0;

                    if(ptr){
                        
                        size_t new_cap = strlen(ptr->strbg) + 1;
                        size_t min_cap = cmd_cap;

                        while(min_cap < new_cap + 1){
                            min_cap *= BUF_GROWTH_FACTOR;
                        }

                        if(new_cap > cmd_cap){
                            if(force_realloc_buf(&cmd, &cmd_cap, &min_cap) == -1) 
                                return NULL; //cmd freed in realloc buf
                        }
                    }
                    
                    if(ptr){
                        strcpy(cmd, ptr->strbg);
                        cmd_idx = cmd_len = strlen(cmd);
                        HANDLE_WRITE_FAIL_FATAL(STDIN_FILENO, cmd, cmd_len, cmd);
                        ptr = ptr->next;
                        continue;
                    } else{
                        ptr = shell->history.head;
                        if(ptr){
                            strcpy(cmd, ptr->strbg);
                            cmd_idx = cmd_len = strlen(cmd);
                            HANDLE_WRITE_FAIL_FATAL(STDIN_FILENO, cmd, cmd_len, cmd);
                            ptr = ptr->next;
                        }
                        continue;
                    }
                } else if(seq_end[1] == 'B'){

                    int i = cmd_idx;
                    while(cmd[i] != '\0'){
                        HANDLE_WRITE_FAIL_FATAL(STDIN_FILENO, "\033[C", 3, cmd);
                        i++;
                    }
                    for(int k = 0; k < cmd_len; k++) HANDLE_WRITE_FAIL_FATAL(STDIN_FILENO, "\b \b", 3, cmd);
                    cmd_idx = cmd_len = 0;

                    if(ptr){
                        size_t new_cap = strlen(ptr->strbg) + 1;
                        size_t min_cap = cmd_cap;

                        while(min_cap < new_cap + 1){
                            min_cap *= BUF_GROWTH_FACTOR;
                        }

                        if(new_cap > cmd_cap){
                            if(force_realloc_buf(&cmd, &cmd_cap, &min_cap) == -1) 
                                return NULL;//cmd freed in
                        }
                    }

                    if(ptr){
                        strcpy(cmd, ptr->strbg);
                        cmd_idx = cmd_len = strlen(cmd);
                        HANDLE_WRITE_FAIL_FATAL(STDIN_FILENO, cmd, cmd_len, cmd);
                        ptr = ptr->prev;
                        continue;
                    } else {
                        ptr = shell->history.head;
                        if(ptr){
                            strcpy(cmd, ptr->strbg);
                            cmd_idx = cmd_len = strlen(cmd);
                            HANDLE_WRITE_FAIL_FATAL(STDIN_FILENO, cmd, cmd_len, cmd);
                            ptr = ptr->prev;
                        }
                        continue;
                    }
                } else if(seq_end[1] == 'C'){
                    if(cmd_idx < cmd_len){
                        char right[] = "\033[C";
                        HANDLE_WRITE_FAIL_FATAL(STDIN_FILENO, right, strlen(right), cmd);
                        cmd_idx++;
                        continue;
                    } else if (suggestion_node) {

                        size_t sugg_len = strlen(suggestion_node->strbg);

                        if (sugg_len >= cmd_cap) {
                        if (force_realloc_buf(&cmd, &cmd_cap, &sugg_len) == -1) 
                            return NULL;
                        }

                        char *to_add = suggestion_node->strbg + cmd_idx;

                        HANDLE_WRITE_FAIL_FATAL(STDIN_FILENO, to_add, strlen(to_add), cmd);
                        strcpy(cmd, suggestion_node->strbg);
                        cmd_idx = cmd_len = sugg_len;
                        suggestion_node = NULL; 
                        continue;
                    }
                } else if(seq_end[1] == 'D'){
                    if(cmd_idx > 0){
                        char left[] = "\033[D";
                        HANDLE_WRITE_FAIL_FATAL(STDIN_FILENO, left, strlen(left), cmd);
                        cmd_idx--;
                        continue;
                    }
                }  
            }
        }
        
        if(c == '\b' || c == 127){
            if(cmd_idx > 0){

                if (cmd_idx == cmd_len) {
                    HANDLE_WRITE_FAIL_FATAL(STDIN_FILENO, "\033[J", 3, cmd);
                }

                if(cmd_idx < cmd_len && cmd_idx > 0){

                    HANDLE_WRITE_FAIL_FATAL(STDIN_FILENO, "\b \b", 3, cmd);

                    size_t count_shift_back = (cmd_len - cmd_idx);

                    memmove(&cmd[cmd_idx - 1], &cmd[cmd_idx], count_shift_back);
                    
                    HANDLE_WRITE_FAIL_FATAL(STDIN_FILENO, &cmd[cmd_idx - 1], count_shift_back, cmd);
                    HANDLE_WRITE_FAIL_FATAL(STDIN_FILENO, "\x1b[C", 3, cmd);
                    HANDLE_WRITE_FAIL_FATAL(STDIN_FILENO, "\b \b", 3, cmd);

                    if (count_shift_back > 0) {
                        char move_cursor_back[MAX_COMMAND_LENGTH];
                        HANDLE_SNPRINTF_FAIL_FATAL(move_cursor_back, "\x1b[%zuD", count_shift_back, cmd)
                        HANDLE_WRITE_FAIL_FATAL(STDIN_FILENO, move_cursor_back, strlen(move_cursor_back), cmd);
                    }
                    
                    cmd_idx--;
                    cmd_len--;
                    cmd[cmd_len] = '\0'; 
                } else{
                    cmd_len--;
                    cmd_idx--;
                    cmd[cmd_len] = '\0';
                    HANDLE_WRITE_FAIL_FATAL(STDIN_FILENO, "\b \b", 3, cmd);
                    HANDLE_WRITE_FAIL_FATAL(STDIN_FILENO, "\033[J", 3, cmd);
                }
                suggestion_node = (cmd_idx == cmd_len) ? search_history(shell, cmd) : NULL;
                render_suggestion(cmd, cmd_len, suggestion_node);
            }

            continue;
        }
        
        if(c == '\n' || c == '\r'){
            HANDLE_WRITE_FAIL_FATAL(STDIN_FILENO, "\033[J", 3, cmd);
            break;
        }

        if (cmd_cap < MAX_COMMAND_LENGTH - 1 && c != '\x1b' && c != '\b') {


            if (cmd_idx == cmd_len) {
                HANDLE_WRITE_FAIL_FATAL(STDIN_FILENO, "\033[J", 3, cmd);
            }
    
            size_t count_to_shift = (cmd_len - cmd_idx);
            if(cmd_cap <= cmd_len + 1){
                size_t new_cap = cmd_cap * BUF_GROWTH_FACTOR;
                if(force_realloc_buf(&cmd, &cmd_cap, &new_cap) == -1) return NULL;
            }

            memmove(&cmd[cmd_idx + 1], &cmd[cmd_idx], count_to_shift);
            cmd[cmd_idx] = c;
            cmd_idx++;
            cmd_len++;
            cmd[cmd_len] = '\0';
            ptr = shell->history.head; 

            HANDLE_WRITE_FAIL_FATAL(STDIN_FILENO, &c, 1, cmd);

            if (count_to_shift > 0) {

                HANDLE_WRITE_FAIL_FATAL(STDIN_FILENO, &cmd[cmd_idx], count_to_shift, cmd);

                char move_cursor_back[32];
                int n = snprintf(move_cursor_back, sizeof(move_cursor_back), "\x1b[%zuD", count_to_shift);
                HANDLE_WRITE_FAIL_FATAL(STDIN_FILENO, move_cursor_back, n, cmd);

                suggestion_node = NULL;
            } else {
                suggestion_node = search_history(shell, cmd);
                render_suggestion(cmd, cmd_len, suggestion_node);
            }
        }
    }

    return cmd;
}