#ifndef TERMINAL_CONTROL_H
#define TERMINAL_CONTROL_H
#include<termios.h>
#include<stdio.h>
#include<unistd.h>
#include<string.h>
#include<stdlib.h>
#include"shell.h"
#include"termstruct.h"

/**
 * @brief resets terminal mode from raw to cooked
 * @param term_ctrl pointer to t_term_ctrl struct
 *
 * This module resets terminal mode from raw mode to cooked mode
 * 
 * @note Driver function must call this after rawify.
 * @warning not calling this is catastrophic.
 */
int reset_terminal_mode(t_shell* shell);

/**
 * @brief sets terminal mode to raw from cooked
 * @param term_ctrl pointer to t_term_ctrl struct
 *
 * This module sets current termios settings to raw mode, disabling ICANON and ECHO flags
 *
 * @note Driver function must call this to rawify terminal settings.
 * @warning not calling this is catastrophic.
 */
int rawify(t_shell* shell);

/**
 * @brief saves terminal settings to cooked for ogl_terminal_settings, raw to new_terminal_settings
 * @param term_ctrl pointer to t_term_ctrl struct
 *
 * This saves current terminal settings into ogl_terminal_settings for cooked mode,
 * saves raw terminal settings into new_terminal_settings for raw mode.
 *
 * @note This is called in init_shell_struct to initialize the terminal struct
 */
int init_s_term_ctrl(t_shell* shell);

#endif // TERMINAL_CONTROL_H