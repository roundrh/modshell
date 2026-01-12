#include "terminal_control.h"

/**
 * @file userinp.c
 * @brief Implementation of functions declared in terminal_control.h
 */

/**
 * @brief resets terminal mode to cooked
 * @param term_ctrl pointer to termios control struct (declared in terminal_control.h)
 * @return 0 on success, -1 on fail
 */
int reset_terminal_mode(t_shell* shell){

    if ( !shell || !isatty(shell->tty_fd) )
        return -1;

    return tcsetattr(shell->tty_fd, TCSANOW, &(shell->term_ctrl.ogl_term_settings));
}

/**
 * @brief sets terminal mode to raw
 * @param term_ctrl pointer to termios control struct (declared in terminal_control.h)
 * @return 0 on success, -1 on fail
 */
int rawify(t_shell* shell) {
    
    if(!shell) 
        return -1;

    shell->term_ctrl.new_term_settings.c_lflag &= ~(ICANON | ECHO);
    shell->term_ctrl.new_term_settings.c_cc[VMIN] = 1;
    shell->term_ctrl.new_term_settings.c_cc[VTIME] = 0;

    return tcsetattr(shell->tty_fd, TCSAFLUSH, &(shell->term_ctrl.new_term_settings));
}

/**
 * @brief initializes t_term_ctrl struct
 * @param term_ctrl pointer to termios control struct (declared in terminal_control.h)
 * @return 0 on success, -1 on fail
 */
int init_s_term_ctrl(t_shell* shell){

    int errstats = tcgetattr(shell->tty_fd, &(shell->term_ctrl.ogl_term_settings));
    shell->term_ctrl.new_term_settings = shell->term_ctrl.ogl_term_settings;

    return errstats;
}