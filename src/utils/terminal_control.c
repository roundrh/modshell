#include "terminal_control.h"

/**
 * @file userinp.c
 * @brief Implementation of functions declared in terminal_control.h
 */

/**
 * @brief resets terminal mode to default
 * @param term_ctrl pointer to termios control struct (declared in
 * terminal_control.h)
 * @return 0 on success, -1 on fail
 */
int reset_terminal_mode(t_shell *shell) {
  if (!shell || !isatty(shell->tty_fd))
    return -1;

  return tcsetattr(shell->tty_fd, TCSANOW, &(shell->term_ctrl.def_settings));
}

/**
 * @brief sets current terminal mode to raw
 * @return 0 on success, -1 on fail
 */
int rawify(t_shell *shell) {

  shell->term_ctrl.curr_settings.c_lflag &= ~(ICANON | ECHO);
  shell->term_ctrl.curr_settings.c_cc[VMIN] = 1;
  shell->term_ctrl.curr_settings.c_cc[VTIME] = 0;

  return tcsetattr(shell->tty_fd, TCSAFLUSH, &(shell->term_ctrl.curr_settings));
}
/*
 * @brief unrawify the shells current terminal state
 */
int unrawify(t_shell *shell) {
  shell->term_ctrl.curr_settings.c_lflag |= (ICANON | ECHO);
  return tcsetattr(shell->tty_fd, TCSAFLUSH, &(shell->term_ctrl.curr_settings));
}

/**
 * @brief initializes t_term_ctrl struct
 * @param term_ctrl pointer to termios control struct (declared in
 * terminal_control.h)
 * @return 0 on success, -1 on fail
 */
int init_s_term_ctrl(t_shell *shell) {

  int errstats = tcgetattr(shell->tty_fd, &(shell->term_ctrl.def_settings));
  tcsetattr(shell->tty_fd, TCSANOW, &shell->term_ctrl.def_settings);

  shell->term_ctrl.curr_settings = shell->term_ctrl.def_settings;

  return errstats;
}
