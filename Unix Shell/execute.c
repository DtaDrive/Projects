#include "dynarray.h"
#include "token.h"
#include "util.h"
#include "lexsyn.h"
#include "snush.h"
#include "execute.h"
#include "job.h"

extern struct job_manager *manager;
extern volatile sig_atomic_t sigchld_flag;
extern volatile sig_atomic_t sigint_flag;

/*--------------------------------------------------------------------*/
void block_signal(int sig, int block) {
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, sig);

    if (sigprocmask(block ? SIG_BLOCK : SIG_UNBLOCK, &set, NULL) < 0) {
    	fprintf(stderr, 
			"[Error] block_signal: sigprocmask(%s, sig=%d) failed: %s\n",
            block ? "SIG_BLOCK" : "SIG_UNBLOCK", sig, strerror(errno));
        exit(EXIT_FAILURE);
    }
}
/*--------------------------------------------------------------------*/
void handle_sigchld(void) {

	/*
	 * Call waitpid() to wait for the child process to terminate.
	 * If the child process terminates, handle the job accordingly.
	*/

	if (!sigchld_flag) return;
    sigchld_flag = 0;

    pid_t pid;
    int status;

    block_signal(SIGCHLD, 1);

    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        for (int i = 0; i < MAX_JOBS; i++) {
            if (manager->jobs[i].job_id == 0) continue;
            if (remove_pid_from_job(&manager->jobs[i], pid)) {
                if (manager->jobs[i].remaining_processes == 0) {
                    if (manager->jobs[i].state == BACKGROUND) {
                        manager->jobs[i].state = FINISHED;
                    } else {
                        delete_job(manager->jobs[i].job_id);
                    }
                }
                break;
            }
        }
    }

    block_signal(SIGCHLD, 0);
	
}
/*--------------------------------------------------------------------*/
void handle_sigint(void) {
	
	/*
	 * Find the foreground job and send signal to every process in the
	 * process group.
	 * handles the SIGINT signal flag and unblock SIGINT.
	 */

	if (!sigint_flag) return;
    sigint_flag = 0;

    block_signal(SIGINT, 1);

    for (int i = 0; i < MAX_JOBS; i++) {
        if (manager != NULL && manager->jobs[i].state == FOREGROUND) {
            if (manager->jobs[i].pgid > 0) {
                killpg(manager->jobs[i].pgid, SIGINT);
            }
            break;
        }
    }

    block_signal(SIGINT, 0);
	 
}
/*--------------------------------------------------------------------*/
void dup2_e(int oldfd, int newfd, const char *func, const int line) {
	int ret;

	ret = dup2(oldfd, newfd);
	if (ret < 0) {
		fprintf(stderr, 
			"Error dup2(%d, %d): %s(%s) at (%s:%d)\n", 
			oldfd, newfd, strerror(errno), errno_name(errno), func, line);
		_exit(127);
	}
}
/*--------------------------------------------------------------------*/
/* check the signals and handle them accordingly. 
 * called in the main loop of snush.c.
 */
void check_signals(void) {
    handle_sigchld();
    handle_sigint();
}
/*--------------------------------------------------------------------*/
void redout_handler(char *fname) {
	int fd;

    fd = open(fname, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        error_print(NULL, PERROR);
        _exit(127);
    }

    dup2_e(fd, STDOUT_FILENO, __func__, __LINE__);
    close(fd);
}
/*--------------------------------------------------------------------*/
void redin_handler(char *fname) {
	int fd;

	fd = open(fname, O_RDONLY);
	if (fd < 0) {
		error_print(NULL, PERROR);
		_exit(127);
	}

	dup2_e(fd, STDIN_FILENO, __func__, __LINE__);
	close(fd);
}
/*--------------------------------------------------------------------*/
void build_command_partial(DynArray_T oTokens, int start, 
						int end, char *args[]) {
	int i, redin = FALSE, redout = FALSE, cnt = 0;
	struct Token *t;

	/* Build command */
	for (i = start; i < end; i++) {
		t = dynarray_get(oTokens, i);

		if (t->token_type == TOKEN_WORD) {
			if (redin == TRUE) {
				redin_handler(t->token_value);
				redin = FALSE;
			}
			else if (redout == TRUE) {
				redout_handler(t->token_value);
				redout = FALSE;
			}
			else {
				args[cnt++] = t->token_value;
			}
		}
		else if (t->token_type == TOKEN_REDIN)
			redin = TRUE;
		else if (t->token_type == TOKEN_REDOUT)
			redout = TRUE;
	}

	if (cnt >= MAX_ARGS_CNT) 
		fprintf(stderr, "[BUG] args overflow! cnt=%d\n", cnt);

	args[cnt] = NULL;

#ifdef DEBUG
	for (i = 0; i < cnt; i++) {
		if (args[i] == NULL)
			printf("CMD: NULL\n");
		else
			printf("CMD: %s\n", args[i]);
	}
	printf("END\n");
#endif
}
/*--------------------------------------------------------------------*/
void build_command(DynArray_T oTokens, char *args[]) {
	build_command_partial(oTokens, 0, 
						dynarray_get_length(oTokens), 
						args);
}
/*--------------------------------------------------------------------*/
int execute_builtin_partial(DynArray_T toks, int start, int end,
                            enum BuiltinType btype, int in_child) {
    
	int argc = end - start;
	struct Token *t1;
	int ret;
    char *dir;

    switch (btype) {
    case B_EXIT:
        if (in_child) return 0;
        
		if (argc == 1) {
			dynarray_map(toks, free_token, NULL);
			dynarray_free(toks);
			exit(EXIT_SUCCESS);
		}
		else {
			error_print("exit does not take any parameters", FPRINTF);
			return -1;
		}

    case B_CD: {
        if (argc == 1) {
            dir = getenv("HOME");
            if (!dir) {
                error_print("cd: HOME variable not set", FPRINTF);
                return -1;
            }
        } 
		else if (argc == 2) {
            t1 = dynarray_get(toks, start + 1);
            if (t1 && t1->token_type == TOKEN_WORD) 
				dir = t1->token_value;
        } 
		else {
            error_print("cd: Too many parameters", FPRINTF);
            return -1;
        }

        ret = chdir(dir);
        if (ret < 0) {
            error_print(NULL, PERROR);
            return -1;
        }
        return 0;
    }

    default:
        error_print("Bug found in execute_builtin_partial", FPRINTF);
        return -1;
    }
}
/*--------------------------------------------------------------------*/
int execute_builtin(DynArray_T oTokens, enum BuiltinType btype) {
	return execute_builtin_partial(oTokens, 0, 
								dynarray_get_length(oTokens), btype, FALSE);
}

void wait_fg(int jobid) {
	pid_t pid;
	int status;

	 // Find the job structure by job ID
    struct job *job = find_job_by_jid(jobid);
    if (!job) {
        fprintf(stderr, "Job: %d not found\n", jobid);
        return;
    }

    while (1) {
        pid = waitpid(-job->pgid, &status, 0);

        if (pid > 0) {
			// Remove the finished process from the job's pid list
			if (!remove_pid_from_job(job, pid)) {
				fprintf(stderr, "Pid %d not found in the job: %d list\n", 
					pid, job->job_id);
			}

			if (job->remaining_processes == 0) break;
        }

        if (pid == 0) continue;

		if (pid < 0) {
			if (errno == EINTR) continue;
			if (errno == ECHILD) break;
			error_print("Unknown error waitpid() in wait_fg()", PERROR);
		}
    }

	// Clean up job table entry if all processes are done
    if (job->remaining_processes == 0)
        delete_job(job->job_id);
}
/*--------------------------------------------------------------------*/
void print_job(int jobid, pid_t pgid) {
    fprintf(stdout, 
		"[%d] Process group: %d running in the background\n", jobid, pgid);
}
/*--------------------------------------------------------------------*/
int fork_exec(DynArray_T oTokens, int is_background) {

	int jobid = -1;
	char *args[MAX_ARGS_CNT];
	char buff;
	
	int pipectrl[2];

	if(pipe(pipectrl)<0){
		error_print("pipe", PERROR);
		return -1;
	}

	pid_t pid = fork();
	if (pid<0){
			error_print("fork", PERROR);
			return -1;
		}

	if(pid == 0){
		close(pipectrl[1]);
		read(pipectrl[0], &buff, 1);
		close(pipectrl[0]);
		setpgid(0, 0);
		tcsetpgrp(STDIN_FILENO, getpid());
		build_command(oTokens, args);
		execvp(args[0], args);
		error_print(args[0], PERROR);
		exit(EXIT_FAILURE);
	}

	if(pid > 0){
		close(pipectrl[0]);
		setpgid(pid, pid);
		jobid = register_job(pid, is_background ? BACKGROUND : FOREGROUND);
		write(pipectrl[1], "g", 1);
		close(pipectrl[1]);

		if(is_background){
			print_job(jobid, pid);
		} else {
			wait_fg(jobid);
			tcsetpgrp(STDIN_FILENO, getpgrp());
		}
	}	

	return jobid;
}
/*--------------------------------------------------------------------*/
int iter_pipe_fork_exec(int n_pipe, DynArray_T oTokens, int is_background) {

	struct Token *t;
	int pipes[n_pipe][2];
	char buff;
	int pipectrl[2];
	char *args[MAX_ARGS_CNT];
	pid_t first_pid;
	int jobid = -1;
	int start_cmd[n_pipe+1];
	int end_cmd[n_pipe+1];
	int index = 0;

	//Initialize pipes
	for(int i = 0; i<n_pipe; i++){
		if(pipe(pipes[i]) < 0){
			error_print("pipe", PERROR);
			return -1;
		}
	}
	if(pipe(pipectrl)<0){
		error_print("pipe", PERROR);
		return -1;
	}

	//Split cmds
	start_cmd[0] = 0;
	for (int i = 0; i< dynarray_get_length(oTokens); i++){
		t = dynarray_get(oTokens, i);
		if (t->token_type == TOKEN_PIPE){
			end_cmd[index] = i;
			index++;
			start_cmd[index] = i+1;
		}
	}
	end_cmd[index] = dynarray_get_length(oTokens);

	//Forking
	for (int i =0; i<=n_pipe; i++){
		pid_t pid = fork();
		if (pid<0){
			error_print("fork", PERROR);
			return -1;
		}
		
		//Child
		if(pid == 0){
			close(pipectrl[1]);
    		read(pipectrl[0], &buff, 1);
    		close(pipectrl[0]);

    		if (i > 0) 
        		dup2_e(pipes[i-1][0], STDIN_FILENO, __func__, __LINE__);
    		if (i < n_pipe) 
        		dup2_e(pipes[i][1], STDOUT_FILENO, __func__, __LINE__);

    		for (int j = 0; j < n_pipe; j++) {
        		close(pipes[j][0]);
       			close(pipes[j][1]);
    		}

    		build_command_partial(oTokens, start_cmd[i], end_cmd[i], args);

    		enum BuiltinType btype = check_builtin(dynarray_get(oTokens, start_cmd[i]));
    		if (btype != NORMAL) {
        		execute_builtin_partial(oTokens, start_cmd[i], end_cmd[i], btype, TRUE);
   	    		exit(EXIT_SUCCESS);
   	 		}

    	execvp(args[0], args);
    	error_print(args[0], PERROR);
    	exit(EXIT_FAILURE);
	}

		//Parent
		if(pid > 0){
			if (i == 0) {
				first_pid = pid;
				setpgid(pid, pid);
				jobid = register_job(first_pid, is_background ? BACKGROUND : FOREGROUND);
			} else{
				setpgid(pid, first_pid);
            	add_pid_to_job(jobid, pid);
			}
			
		}
	}

	close(pipectrl[0]);
    write(pipectrl[1], "x", 1);
    close(pipectrl[1]);

	for (int i = 0; i < n_pipe; i++) {
    close(pipes[i][0]);
    close(pipes[i][1]);
    }

	if(is_background){
			print_job(jobid, first_pid);
		} else {
			wait_fg(jobid);
			tcsetpgrp(STDIN_FILENO, getpgrp());
		}

	return jobid;
}
/*--------------------------------------------------------------------*/