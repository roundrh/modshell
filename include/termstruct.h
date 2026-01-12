#ifndef TERMSTRUCT_H
#define TERMSTRUCT_H

#include<stdio.h>
#include<unistd.h>
#include<string.h>
#include<stdlib.h>
#include<termios.h>

/**
 * @file terminal_control.h
 * @brief Handles switch from raw to cooked termios
 *
 * Module declares helper functions to handle switching resetting and initializing termios structs 
 * encapsulated within a struct.
 */

/**
 * @typedef struct s_term_ctrl t_term_ctrl
 * @brief contains ogl_term_settings for cooked termios settings, new_term_settings for raw termios settings. 
 */
typedef struct s_term_ctrl{
    struct termios ogl_term_settings;
    struct termios new_term_settings;
} t_term_ctrl;


#endif // ! TERMSTRUCT_H