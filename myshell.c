#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/wait.h>

int prepare() /* preapare before process_arglist */
{

    /* zombie-sig_chld handler: when encounters SIGCHILD do SIG_IGN - ignore */
    /* http://www.microhowto.info/howto/reap_zombie_processes_using_a_sigchld_handler.html taken from*/
    struct sigaction sa;
    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGCHLD, &sa, 0) == -1) { 
        fprintf(stderr, "zombie-handling signal error %s\n", strerror(errno)); /* print zombie-handling error to stderr */
        exit(1);
    }

    /* our default set for SIGINT will be SIG_IGN, if we know its foregroundchild, then update the handler to SIG_DFL and terminate by that */
    /* sigint shell handler - unless we know its foreground child, default set we use is: SIGINT -> SIG_IGN*/
    /* https://www.youtube.com/watch?v=jF-1eFhyz1U&t=220s here learned how to create the handler */
    int state_shell;
    state_shell = 0;
    struct sigaction shell_sigi;
    shell_sigi.sa_handler = SIG_IGN; /* the parent (shell) and backchild should not terminate upon SIGINT */
    shell_sigi.sa_flags = SA_RESTART; /* avoiding the EINTR, SA_RESTART lets handler work acoording to documentation in https://www.gnu.org/software/libc/manual/html_node/Flags-for-Sigaction.html and the video in up link */
    state_shell = sigaction(SIGINT, &shell_sigi, NULL); /* if the code sees SIGINT go to shell_sigi handler which will lead you to SIG_IGN */
    if (state_shell != 0) /* if handle fails */
    {
        fprintf(stderr, "shell signal error %s\n", strerror(errno)); /* print sigint_handling error to stderr */
        exit(1);
    }
    return 0; /* 0 - success, other - else */

}

int finalize() /* cleanups after process_arglist */
{
    return 0; /* returns 0 on success */
}

/* returns the index of the word "|" in arg list. -1 if "|" not in arglist*/
int arglist_pipe_index(int count, char **arglist)
{
    int i;
    for(i=0; i<count; i++)
    {
        if (strcmp(arglist[i], "|") == 0) /* if the word is pipe "|" */
        {
            return i;
        }
    }
    return -1; /* word has no pipe */
}

/* returns the index of the word ">>" in arg list. -1 if ">>" not in arglist*/
int arglist_redirection_index(int count, char **arglist)
{
    int i;
    for(i=0; i<count; i++)
    {
        if (strcmp(arglist[i], ">>") == 0) /* if the word is pipe ">>" */
        {
            return i;
        }
    }
    return -1; /* word has no redirection */
}

/* in foreground child, we update the SIGINT handler to terminate */
void sigint_foreground_child_handler() /* Foreground child processes (regular commands or parts of a pipe) should terminate upon SIGINT*/
{   
    /* SIGINT handler that terminates the procces by sig_dfl: if foregroundcild sees SIGINT got to SIG_DFL and terminate*/
    /* https://www.youtube.com/watch?v=jF-1eFhyz1U&t=220s here learned how to create the handler */
    int state_sigint;
    state_sigint = 0;
    struct sigaction foreground_sigi;
    foreground_sigi.sa_handler = SIG_DFL; /* The default action is to terminate the process */
    foreground_sigi.sa_flags = SA_RESTART;  /* avoiding the EINTR, SA_RESTART lets handler work acoording to documentation in https://www.gnu.org/software/libc/manual/html_node/Flags-for-Sigaction.html and the video in up link */
    state_sigint = sigaction(SIGINT, &foreground_sigi, NULL);
    if (state_sigint != 0) /* if handle fails */
    {
        fprintf(stderr, "foreground-handling signal error %s\n", strerror(errno)); /* print foreground-handling error to stderr */
        exit(1);
    }
    return;
}


int process_arglist(int count, char **arglist)
{
    int pid; 
    int status;
    int exe_failed;
    char* child_name;
    int pipe_index;
    int redirection_index;

    child_name = arglist[0];
    pipe_index = arglist_pipe_index(count, arglist); /* finds the index pipe in arglist, -1 if not inside */
    redirection_index = arglist_redirection_index(count, arglist); /* finds the redirection index in arglist, -1 if not inside */

    /*dividing into 4 cases:*/
    /* case 2: Executing commands in the background */
    if (strcmp(arglist[count-1], "&") == 0) /* if last word before null is & */
    {
        pid = fork(); /* create child */
        if (pid < 0) /* if fork failed-error */
        {
            fprintf(stderr, "fork error %s\n", strerror(errno)); /* print fork error to stderr */
            return 0; /* error -> return 0 */
        }   
        /* child code */
        if (pid == 0)
        {
            arglist[count-1] = NULL; /* delete '&' and set last value to NULL */
            exe_failed = execvp(child_name, arglist); /* replace created child by new process of given command */
            if (exe_failed == -1) /* if execvp failed-error */
            {
                fprintf(stderr, "invalid user command/ file permission error %s\n", strerror(errno)); /* print execvp error to stderr */
                exit(0); /* error -> return 0 */
            }           
        }
    }

    /* case 3: Piping */
    /* https://www.youtube.com/watch?v=6xbLgZpOBi8 learned here about piping in C */
    else if (pipe_index != -1) /* if arglist contains the word "|" */
    {
        int pid_command1;
        int pid_command2;
        int pfds[2];
        int pipe_fail;
        pipe_fail = pipe(pfds); /* create the pipe */   
        if (pipe_fail == -1) /* if pipe error */
        {
            fprintf(stderr, "pipe creation error %s\n", strerror(errno)); /* print pipe creation error to stderr */
            return 0; /* error -> return 0 */
        }
        /* creating command1 child: the writer */
        pid_command1 = fork(); /* create child1: writer */
        if (pid_command1 < 0) /* if fork failed-error */
        {
            fprintf(stderr, "fork error %s\n", strerror(errno)); /* print fork error to stderr */
            return 0; /* error -> return 0 */
        }   
        /* child1- command1 code */
        if (pid_command1 == 0)
        {
            sigint_foreground_child_handler(); /* setting sigint handler for foreground child */
            if (dup2(pfds[1], 1) == -1) /* want to write to right stdout */
            {
                fprintf(stderr, "dup2 error %s\n", strerror(errno)); /* print dup2 error to stderr */
                exit(0); /* error -> return 0 */
            }
            close(pfds[1]); /* we dont need it thanks to dup */
            close(pfds[0]); /* close read edge of pipe because now we write */
            arglist[pipe_index] = NULL; /* just for the next line, adjust the arglist so it will contain just command1 */
            exe_failed = execvp(child_name, arglist); /* replace created child by new process of given command1 */
            arglist[pipe_index] = "|"; /* return arglist to normal */
            if (exe_failed == -1) /* if execvp failed-error */
            {
                fprintf(stderr, "invalid user command/ file permission error %s\n", strerror(errno)); /* print execvp error to stderr */
                exit(0); /* error -> return 0 */
            }           
        }
        /* parent code */
        pid_command2 = fork(); /* create child2: reader */
        if (pid_command2 < 0) /* if fork failed-error */
        {
            fprintf(stderr, "fork error %s\n", strerror(errno)); /* print fork error to stderr */
            return 0; /* error -> return 0 */
        } 
        /* child2-command2 code */
        if (pid_command2 == 0)
        {
            sigint_foreground_child_handler(); /* setting sigint handler for foreground child */
            if(dup2(pfds[0], 0) == -1) /* want to read to right stdin */
            {
                fprintf(stderr, "dup2 error %s\n", strerror(errno)); /* print dup2 error to stderr */
                exit(0); /* error -> return 0 */
            }
            close(pfds[0]); /* close read edge of pipe because we did dup2 */
            close(pfds[1]); /* close write because now we read */
            exe_failed = execvp(arglist[pipe_index+1], &arglist[pipe_index+1]); /* replace created child by new process of given command2, noticed by indexes of arglist */
            if (exe_failed == -1) /* if execvp failed-error */
            {
                fprintf(stderr, "invalid user command/ file permission error %s\n", strerror(errno)); /* print execvp error to stderr */
                exit(0); /* error -> return 0 */
            }           
        }
        /* parent code */
        close(pfds[0]); /* close read edge at end */
        close(pfds[1]); /* close write edge at end */
        /* wait for both commands to finish before accepting new commands */
        /* wait for command1 */
        if (waitpid(pid_command1, &status, 0) == -1) /* wait for command1 and ignore ECHILD */
        {
            if(errno != ECHILD) /* ignore ECHILD as wanted */
            {
                fprintf(stderr, "waitpid error %s\n", strerror(errno)); /* print waitpid error to stderr */
                return 0; /* error -> return 0 */
            }
        }
        /* wait for command2 */
        if (waitpid(pid_command2, &status, 0) == -1) /* wait for command2 and ignore ECHILD */
        {
            if(errno != ECHILD) /* ignore ECHILD as wanted */
            {
                fprintf(stderr, "waitpid error %s\n", strerror(errno)); /* print waitpid error to stderr */
                return 0; /* error -> return 0 */
            }
        }
    }

    /* case 4: Output redirection */
    /* https://www.youtube.com/watch?v=5fnVr-zH-SE&t=186s here learned about output redirection in C*/
    else if (redirection_index != -1) /* if arglist has the word ">>" */
    {
        int file; /* our file descriptor */
        pid = fork(); /* create child */
        if (pid < 0) /* if fork failed-error */
        {
            fprintf(stderr, "fork error %s\n", strerror(errno)); /* print fork error to stderr */
            return 0; /* error -> return 0 */
        }   
        /* child code */
        if (pid == 0)
        {
            /* open file on append mode. if doesn't exist-> create it, 0600 only I-owner can write/read */
            /* filename is right after ">>" */
            file = open(arglist[redirection_index+1], O_CREAT|O_APPEND|O_WRONLY,0600);
            if (file == -1) /* if open failed-error */
            {
                fprintf(stderr, "open/create file error %s\n", strerror(errno)); /* print open/create error to stderr */
                exit(0); /* error -> return 0 */
            }       
            if (dup2(file, 1) == -1) /* set our file descriptor instead of stdout */
            {
                fprintf(stderr, "dup2 error %s\n", strerror(errno)); /* print dup2 error to stderr */
                exit(0); /* error -> return 0 */
            }
            close(file); /* close the file descriptor since we use the original 1 now */

            arglist[redirection_index] = NULL; /* just for the next line, adjust the arglist so it will contain just the command, not file */
            exe_failed = execvp(child_name, arglist); /* replace created child by new process of given command */
            arglist[redirection_index] = ">>"; /* return arglist to normal */
            if (exe_failed == -1) /* if execvp failed-error */
            {
                fprintf(stderr, "invalid user command/ file permission error %s\n", strerror(errno)); /* print execvp error to stderr */
                exit(0); /* error -> return 0 */
            }  
        }
        if (waitpid(pid, &status, 0) == -1) /* parent has to wait for son to finish - don't want zombies, ignore ECHILD */
        {
            if(errno != ECHILD) /* ignore ECHILD as wanted */
            {
                fprintf(stderr, "waitpid error %s\n", strerror(errno)); /* print waitpid error to stderr */
                return 0; /* error -> return 0 */
            }
        }
    }

    /* case 1: regular case */
    else 
    {
        pid = fork(); /* create child */
        if (pid < 0) /* if fork failed-error */
        {
            fprintf(stderr, "fork error %s\n", strerror(errno)); /* print fork error to stderr */
            return 0; /* error -> return 0 */
        }
        /* child code */
        if (pid == 0)
        {
            sigint_foreground_child_handler(); /* setting sigint handler for foreground child */
            exe_failed = execvp(child_name, arglist); /* replace created child by new process of given command */
            if (exe_failed == -1) /* if execvp failed-error */
            {
                fprintf(stderr, "invalid user command/ file permission error %s\n", strerror(errno)); /* print execvp error to stderr */
                exit(0); /* error -> return 0 */
            }
            
        }
        /* parent code */
        else 
        {   
            if (waitpid(pid, &status, 0) == -1) /* parent has to wait for son to finish and ignore ECHILD */
            {
                if(errno != ECHILD) /* ignore ECHILD as wanted */
                {
                    fprintf(stderr, "waitpid error %s\n", strerror(errno)); /* print waitpid error to stderr */
                    return 0; /* error -> return 0 */
                }
            }
        }
    }

    return 1; /* return 1 if no errors */
}






