#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <dirent.h>
#include <sys/file.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <signal.h>
#include <unistd.h>
#include <paths.h>

#include <crt_externs.h>

#include <tcl.h>

#define BUFSIZ 1024

static int ui_info(Tcl_Interp *interp, char *mesg) {
	const char ui_proc[] = "ui_info {";
	char *script, *p;
	int scriptlen, ret;

	scriptlen = sizeof(ui_proc) + strlen(mesg);
	script = malloc(scriptlen);
	if (script == NULL)
		return TCL_ERROR;
	else
		p = script;

	memcpy(script, ui_proc, sizeof(ui_proc));
	strcat(script, mesg);
	p += scriptlen - 2;
	*p = '}';
	return (Tcl_EvalEx(interp, script, scriptlen - 1, 0));
}

#define environ *(_NSGetEnviron())
static struct pid {
	struct pid *next;
	FILE *fp;
	pid_t pid;
} *pidlist;

FILE *
dup2popen (command, type) /*stderr and stdout together in the output*/
	const char * command, *type;
{
        struct pid *cur;
        FILE *iop;
        int pdes[2], pid, twoway;
        char *argv[4];
        struct pid *p;

        if (strchr(type, '+')) {
                twoway = 1;
                type = "r+";
                if (socketpair(AF_UNIX, SOCK_STREAM, 0, pdes) < 0)
                        return (NULL);
        } else  {
                twoway = 0;
                if ((*type != 'r' && *type != 'w') || type[1])
                        return (NULL);
        }
        if (pipe(pdes) < 0)
                return (NULL);

        if ((cur = malloc(sizeof(struct pid))) == NULL) {
                (void)close(pdes[0]);
                (void)close(pdes[1]);
                return (NULL);
        }

        argv[0] = "sh";
        argv[1] = "-c";
        argv[2] = (char *)command;
        argv[3] = NULL;

        switch (pid = vfork()) {
        case -1:                        /* Error. */
                (void)close(pdes[0]);
                (void)close(pdes[1]);
                free(cur);
                return (NULL);
                /* NOTREACHED */
        case 0:                         /* Child. */
                if (*type == 'r') {
                        /*
                         * The _dup2() to STDIN_FILENO is repeated to avoid
                         * writing to pdes[1], which might corrupt the
                         * parent's copy.  This isn't good enough in
                         * general, since the _exit() is no return, so
                         * the compiler is free to corrupt all the local
                         * variables.
                         */
                        (void)close(pdes[0]);
                        if (pdes[1] != STDOUT_FILENO) {
                                (void)dup2(pdes[1], STDOUT_FILENO);
                                (void)close(pdes[1]);
                                if (twoway) 
                                        (void)dup2(STDOUT_FILENO, STDIN_FILENO);
                        } else if (twoway && (pdes[1] != STDIN_FILENO)) 
                                (void)dup2(pdes[1], STDIN_FILENO);
                } else {
                        if (pdes[0] != STDIN_FILENO) {
                                (void)dup2(pdes[0], STDIN_FILENO);
                                (void)close(pdes[0]);
                        }
                        (void)close(pdes[1]);
                        }
                for (p = pidlist; p; p = p->next) {
                        (void)close(fileno(p->fp));
                }
		(void)dup2(STDOUT_FILENO,STDERR_FILENO);
                execve(_PATH_BSHELL, argv, environ);
                _exit(127);
                /* NOTREACHED */
        }

        /* Parent; assume fdopen can't fail. */
        if (*type == 'r') {
                iop = fdopen(pdes[0], type);
                (void)close(pdes[1]);
        } else {
                iop = fdopen(pdes[1], type);
                (void)close(pdes[0]);
        }

        /* Link into list of file descriptors. */
        cur->fp = iop;
        cur->pid =  pid;
        cur->next = pidlist;
        pidlist = cur;

        return (iop);
}


int SystemCmd(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
	char buf[BUFSIZ];
	char *cmdstring, *p;
	FILE *pipe;
	
	int i, cmdlen, cmdlenavail;
	cmdlen = cmdlenavail = BUFSIZ;
	p = cmdstring = NULL;

	if(Tcl_PkgRequire(interp, "portui", "1.0", 0) == NULL) {
		return TCL_ERROR;
	}

	if (objc < 2) {
		Tcl_WrongNumArgs(interp, 1, objv, "command");
		return TCL_ERROR;
	} else if (objc == 2) {
		cmdstring = Tcl_GetString(objv[1]);
	} else if (objc > 2) {
		cmdstring = malloc(cmdlen);
		if (cmdstring == NULL)
			return TCL_ERROR;
		p = cmdstring;
		/*
		 * Rather than realloc for every iteration
		 * through the argument vector, malloc a
		 * sizable chunk of memory first.
		 * If we extend beyond what is available,
		 * then realloc
		 */
		for (i = 1; i < objc; i++) {
			char *arg;
			int len;

			arg = Tcl_GetString(objv[i]);
			/* Add 1 for trailing \0 or ' ' */
			len = strlen(arg) + 1;

			if (len > cmdlenavail) {
				char *sptr;
				cmdlen += cmdlenavail + len;
				/*
				 * puma does not have reallocf.
				 * Change when we rev past puma
				 */
				sptr = cmdstring;
				cmdstring = realloc(cmdstring, cmdlen);
				if (cmdstring == NULL) {
					free(sptr);
					return TCL_ERROR;
				}
			}
			/* Subtract 1 to not copy trailing \0 */
			memcpy(p, arg, len - 1);
			p += len;

			if (i == objc - 1) {
				*(p - 1) = '\0';
			} else {
				*(p - 1) = ' ';
			}
			cmdlenavail -= len;
			cmdlen += len;
		}
	}

	/*pipe = popen(cmdstring, "r");*/
	pipe = dup2popen(cmdstring, "r");
	/*Gutted the popen code...*/
	if (p != NULL)
		free(cmdstring);

	while (fgets(buf, BUFSIZ, pipe) != NULL) {
		int ret = ui_info(interp, buf);
		if (ret != TCL_OK)
			return ret;
		Tcl_AppendResult(interp, buf, NULL);
	}

	switch (pclose(pipe)) {
		case 0:
			return TCL_OK;
		default:
			return TCL_ERROR;
	}
}

int FlockCmd(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
	static const char errorstr[] = "use one of \"-shared\", \"-exclusive\", or \"-unlock\"";
	int operation = 0, fd, i;
	Tcl_Channel channel;
	ClientData handle;

	if (objc < 3 || objc > 4) {
		Tcl_WrongNumArgs(interp, 1, objv, "channelId switches");
		return TCL_ERROR;
	}

    	if ((channel = Tcl_GetChannel(interp, Tcl_GetString(objv[1]), NULL)) == NULL)
		return TCL_ERROR;

	if (Tcl_GetChannelHandle(channel, TCL_READABLE|TCL_WRITABLE, &handle) != TCL_OK) {
		Tcl_SetResult(interp, "error getting channel handle", TCL_STATIC);
		return TCL_ERROR;
	}
	fd = (int) handle;

	for (i = 2; i < objc; i++) {
		char *arg = Tcl_GetString(objv[i]);
		if (!strcmp(arg, "-shared")) {
			if (operation & LOCK_EX || operation & LOCK_UN) {
				Tcl_SetResult(interp, (void *) &errorstr, TCL_STATIC);
				return TCL_ERROR;
			}
			operation |= LOCK_SH;
		} else if (!strcmp(arg, "-exclusive")) {
			if (operation & LOCK_SH || operation & LOCK_UN) {
				Tcl_SetResult(interp, (void *) &errorstr, TCL_STATIC);
				return TCL_ERROR;
			}
			operation |= LOCK_EX;
		} else if (!strcmp(arg, "-unlock")) {
			if (operation & LOCK_SH || operation & LOCK_EX) {
				Tcl_SetResult(interp, (void *) &errorstr, TCL_STATIC);
				return TCL_ERROR;
			}
			operation |= LOCK_UN;
		} else if (!strcmp(arg, "-noblock")) {
			if (operation & LOCK_UN) {
				Tcl_SetResult(interp, "-noblock can not be used with -unlock", TCL_STATIC);
				return TCL_ERROR;
			}
			operation |= LOCK_NB;
		}
	}
	if (flock(fd, operation) != 0)
	{
		Tcl_SetResult(interp, (void *) strerror(errno), TCL_STATIC);
		return TCL_ERROR;
	}
	return TCL_OK;
}

int ReaddirCmd(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
	DIR *dirp;
	struct dirent *dp;
	Tcl_Obj *tcl_result;
	char *path;

	if (objc != 2) {
		Tcl_WrongNumArgs(interp, 1, objv, "directory");
		return TCL_ERROR;
	}

	path = Tcl_GetString(objv[1]);
	dirp = opendir(path);
	if (!dirp) {
		Tcl_SetResult(interp, "Directory not found", TCL_STATIC);
		return TCL_ERROR;
	}
	tcl_result = Tcl_NewListObj(0, NULL);
	while (dp = readdir(dirp)) {
		Tcl_ListObjAppendElement(interp, tcl_result, Tcl_NewStringObj(dp->d_name, -1));
	}
	closedir(dirp);
	Tcl_SetObjResult(interp, tcl_result);
	
	return TCL_OK;
}

int Pextlib_Init(Tcl_Interp *interp)
{
	Tcl_CreateObjCommand(interp, "system", SystemCmd, NULL, NULL);
	Tcl_CreateObjCommand(interp, "flock", FlockCmd, NULL, NULL);
	Tcl_CreateObjCommand(interp, "readdir", ReaddirCmd, NULL, NULL);
	if(Tcl_PkgProvide(interp, "Pextlib", "1.0") != TCL_OK)
		return TCL_ERROR;
	return TCL_OK;
}
