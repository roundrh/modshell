#include"sigtable_cleanup.h"

/**
 * @file sigtable_cleanup.c
 * @brief implements cleanup function for sigtable
*/

/**
 * @brief cleans up sigtable restoring default handlers
 * @param sigtable pointer to t_shell_sigtable parent struct
 * @return 0 on success, -1 on fail
 *
 * Function resets all signals called in initializer to default handlers and flags in oldact.
 */
int cleanup_sigtable(t_shell_sigtable* sigtable){

    if(sigtable == NULL)
        return -1;

    if(sigaction(SIGINT,  &sigtable->sigint.oldact, NULL) == -1){
        perror("sigaction cleanup SIGINT");
        return -1;
    }
    if(sigaction(SIGQUIT, &sigtable->sigquit.oldact, NULL) == -1){
        perror("sigaction cleanup SIGQUIT");
        return -1;
    }
    if(sigaction(SIGCHLD, &sigtable->sigchld.oldact, NULL) == -1){
        perror("sigaction cleanup SIGCHLD");
        return -1;
    }
    if(sigaction(SIGTSTP, &sigtable->sigtstp.oldact, NULL) == -1){
        perror("sigaction cleanup SIGTSTP");
        return -1;
    }
    if(sigaction(SIGTTOU, &sigtable->sigttou.oldact, NULL) == -1){
        perror("sigaction cleanup SIGTTOU");
        return -1;
    }
    if(sigaction(SIGTTIN, &sigtable->sigttin.oldact, NULL) == -1){
        perror("sigaction cleanup SIGTTIN");
        return -1;
    }

    return 0;
}