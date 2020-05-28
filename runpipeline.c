#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <errno.h>


#define MAX_NUM_PROGRAMS 10
#define FD_STDIN    0
#define FD_STDOUT   1
#define PIPEFD_READ     0
#define PIPEFD_WRITE    1

void die(char *s)
{
    fprintf(stderr, "Error: %s\n", s);
    if (errno)
        perror("errno");
    exit(EXIT_FAILURE);
} 

void check_pointer(void *p)
{
    if (p == NULL)
       die("Pointer is NULL.");
}

void check_return_value(int rv, char *s)
{
    if (rv < 0)
        die(s);
}


// structure for storing program arguments
typedef struct program_tag {
    char**   argv;      // array of pointers to arguments
    int      argc;      // number of arguments
    int	     pid;	    // process ID of the program
    int      fd_in;     // FD for stdin
    int      fd_out;    // FD for stdout
} Program;

/* start a program. This function does not wait for the child process to 
 * finish.
 *
 * This function needs to fork, redirect stdin/stdout, and then upgrade
 * the child process
 *
 * Parameters:
 *      programs: an array of Program structures.
 *      num_programs: number of programs in the array.
 *      cur: the index of the program that needs to be started in this call
 */
void start_program(Program *programs, int num_programs, int cur) 
{
 
    Program *prog = &programs[cur];
    int rv;

    pid_t child = fork();

    if (child < 0) {
        die("fork() failed.");
    } else if (child == 0) { /* Child */
        
        if (cur > 0) {
            rv = dup2(prog->fd_in, FD_STDIN);
            check_return_value(rv, "dup2()");
        }
        if (cur != num_programs - 1) {
            rv = dup2(prog->fd_out, FD_STDOUT);
            check_return_value(rv, "dup2()");
        }

        // close all other pipe FDs

        for (int i = cur; i < num_programs; i ++) {
            if (programs[i].fd_in >= 0) {
                rv = close(programs[i].fd_in);
                check_return_value(rv, "close()");
            }
            if (programs[i].fd_out >= 0) {
                rv = close(programs[i].fd_out);
                check_return_value(rv, "close()");
            }
        }

        execvp(prog->argv[0], prog->argv);
        die("execvp() failed.");
    } else { /* parent */
        prog->pid = child;

    
        if (cur > 0 && prog->fd_in >= 0) {
            rv = close(prog->fd_in);
            check_return_value(rv, "close()");
        }
        if (cur != num_programs - 1 && prog->fd_out >= 0) {
            rv = close(prog->fd_out);
            check_return_value(rv, "close()");
        }

        prog->fd_in = -1;
        prog->fd_out = -1;
    }
   
}

/* Wait on a program. 
 *
 * Parameter:
 *  prog:   A pointer to a Program, which has the pid of the program,
 *          which is set when the child process is created.
 *
 * Return value:
 *  -1:     prog->pid is less than 0, or waitpid() returns error.
 *  0-255:  exit value of the program (after WEXITSTATUS()).
 *
 * */
int wait_on_program(Program *prog)
{
    int exitStatus;

    if (prog->pid < 0)
        return -1;


    int rv = waitpid(prog->pid, &exitStatus, 0);
    if (rv != prog->pid)
        return -1;
  
    return WEXITSTATUS(exitStatus);
}

/* This function creates pipes to be used for connecting two pipeline stages.
 *
 * Paramters:
 *  programs: an array of Program structures.
 *  num_programs: number of programs in the array.
 *
 * Return value:
 *  This function does not return a value.
 */
void prepare_pipes(Program *programs, int num_programs)
{
   
    int fd[2];

    for (int i = 1; i < num_programs; i ++) {
        int status = pipe(fd);
        if (status != 0) 
            die("pipe() failed.");
        programs[i-1].fd_out = fd[PIPEFD_WRITE];
        programs[i].fd_in = fd[PIPEFD_READ];
    }
   
}


/* initialize a Program structure that allows argc arguments */
void init_program(Program *prog, int argc)
{
    // allocate memory for array of arguments
    prog->argv = malloc((argc + 1) * sizeof(char *));
    check_pointer(prog->argv);
    prog->argc= 0;
    prog->argv[0] = NULL;
    prog->pid = prog->fd_in = prog->fd_out = -1;
}

/* free the memory used by prog */
void free_programs(Program *programs, int num_programs)
{
    for (int i = 0; i <  num_programs; i ++) {
        free(programs[i].argv);
        programs[i].argv = NULL;
    }
}

// parse the command line arguments
// split them into programs
// return the number of programs found
int parse_command_line(Program *progs, int max_num_progs, int argc, char **argv)
{
    int     cur_arg = 1;
    int     prog_count = 0;

    while (cur_arg < argc) {
    

        if (prog_count == max_num_progs) 
            die("Too many programs.");

        // allocate space for argv for progs[prog_count]
 
        init_program(&progs[prog_count], argc);

        int     ia = 0;  // index of arguments for progs[prog_count]
        while (cur_arg < argc && strcmp(argv[cur_arg], "--")) {
            progs[prog_count].argv[ia++] = argv[cur_arg++];
        }
        progs[prog_count].argv[ia] = NULL;
        progs[prog_count].argc = ia;
        prog_count ++;
        if (ia == 0) 
            die("Empty program.");

        cur_arg ++; // skip "--"
        if (cur_arg == argc) 
            die("Last program is empty."); 
    }

    return prog_count;
}

int main(int argc, char **argv)
{
    Program programs[MAX_NUM_PROGRAMS];
    int     num_programs;

    if (argc <= 1) 
        die ("Specify at least one program to run. Multiple programs are separated by --");

    // Prepare programs and their arguments
    num_programs = parse_command_line(programs, MAX_NUM_PROGRAMS, argc, argv); 

    // Prepare pipes
    prepare_pipes(programs, num_programs);

    // spawn children
    for (int i = 0; i < num_programs; i ++) {
        fprintf(stderr, "Starting program %d:%s\n", i, programs[i].argv[0]);
        start_program(programs, num_programs, i);
    }

    // wait for children
    for (int i = 0; i < num_programs; i ++) {
        fprintf(stderr, "Waiting for program %d:%s\n", i, programs[i].argv[0]);
        int status = wait_on_program(&programs[i]);
        fprintf(stderr, "Program %d:%s exited with %d\n", i, programs[i].argv[0], status);
    }

    // free memory
    free_programs(programs, num_programs);

    // You should see the message only once
    fprintf(stderr, "Parent: Everything is good.\n");
    return 0;
}
