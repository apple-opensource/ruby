/**********************************************************************

  process.c -

  $Author: jkh $
  $Date: 2002/05/27 17:59:44 $
  created at: Tue Aug 10 14:30:50 JST 1993

  Copyright (C) 1993-2000 Yukihiro Matsumoto
  Copyright (C) 2000  Network Applied Communication Laboratory, Inc.
  Copyright (C) 2000  Information-technology Promotion Agency, Japan

**********************************************************************/

#include "ruby.h"
#include "rubysig.h"
#include <stdio.h>
#include <errno.h>
#include <signal.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifndef NT
#ifdef HAVE_SYS_TIME_H
# include <sys/time.h>
#else
struct timeval {
        long    tv_sec;         /* seconds */
        long    tv_usec;        /* and microseconds */
};
#endif
#endif /* NT */
#include <ctype.h>

struct timeval rb_time_interval _((VALUE));

#ifdef HAVE_SYS_WAIT_H
# include <sys/wait.h>
#endif
#ifdef HAVE_GETPRIORITY
# include <sys/resource.h>
#endif
#include "st.h"

#ifdef __EMX__
#undef HAVE_GETPGRP
#endif

static VALUE
get_pid()
{
    return INT2FIX(getpid());
}

static VALUE
get_ppid()
{
#ifdef NT
    return INT2FIX(0);
#else
    return INT2FIX(getppid());
#endif
}

VALUE rb_last_status = Qnil;

#if !defined(HAVE_WAITPID) && !defined(HAVE_WAIT4)
#define NO_WAITPID
static st_table *pid_tbl;
#endif

static int
rb_waitpid(pid, flags, st)
    int pid;
    int flags;
    int *st;
{
    int result;
#ifndef NO_WAITPID
    int oflags = flags;
    if (!rb_thread_alone()) {	/* there're other threads to run */
	flags |= WNOHANG;
    }

  retry:
    TRAP_BEG;
#ifdef HAVE_WAITPID
    result = waitpid(pid, st, flags);
#else  /* HAVE_WAIT4 */
    result = wait4(pid, st, flags, NULL);
#endif
    TRAP_END;
    if (result < 0) {
	if (errno == EINTR) {
	    rb_thread_polling();
	    goto retry;
	}
	return -1;
    }
    if (result == 0) {
	if (oflags & WNOHANG) return 0;
	rb_thread_polling();
	if (rb_thread_alone()) flags = oflags;
	goto retry;
    }
#else  /* NO_WAITPID */
    if (pid_tbl && st_lookup(pid_tbl, pid, st)) {
	rb_last_status = INT2FIX(*st);
	st_delete(pid_tbl, &pid, NULL);
	return pid;
    }

    if (flags) {
	rb_raise(rb_eArgError, "can't do waitpid with flags");
    }

    for (;;) {
	TRAP_BEG;
	result = wait(st);
	TRAP_END;
	if (result < 0) {
	    if (errno == EINTR) {
		rb_thread_schedule();
		continue;
	    }
	    return -1;
	}
	if (result == pid) {
	    break;
	}
	if (!pid_tbl)
	    pid_tbl = st_init_numtable();
	st_insert(pid_tbl, pid, st);
	if (!rb_thread_alone()) rb_thread_schedule();
    }
#endif
    rb_last_status = INT2FIX(*st);
    return result;
}

#ifdef NO_WAITPID
struct wait_data {
    int pid;
    int status;
};

static int
wait_each(key, value, data)
    int key, value;
    struct wait_data *data;
{
    if (data->status != -1) return ST_STOP;

    data->pid = key;
    data->status = value;
    return ST_DELETE;
}
#endif

static VALUE
proc_wait()
{
    int pid, state;
#ifdef NO_WAITPID
    struct wait_data data;

    data.status = -1;
    st_foreach(pid_tbl, wait_each, &data);
    if (data.status != -1) {
	rb_last_status = data.status;
	return INT2FIX(data.pid);
    }

    while (1) {
	TRAP_BEG;
	pid = wait(&state);
	TRAP_END;
	if (pid >= 0) break;
        if (errno == EINTR) {
            rb_thread_schedule();
            continue;
        }
        rb_sys_fail(0);
    }
    rb_last_status = INT2FIX(state);
#else
    if ((pid = rb_waitpid(-1, 0, &state)) < 0)
	rb_sys_fail(0);
#endif
    return INT2FIX(pid);
}

static VALUE
proc_wait2()
{
    VALUE pid = proc_wait();

    return rb_assoc_new(pid, rb_last_status);
}

static VALUE
proc_waitpid(argc, argv)
    int argc;
    VALUE *argv;
{
    VALUE vpid, vflags;
    int pid, flags, status;

    flags = 0;
    rb_scan_args(argc, argv, "11", &vpid, &vflags);
    if (argc == 2 && !NIL_P(vflags)) {
	flags = NUM2UINT(vflags);
    }

    if ((pid = rb_waitpid(NUM2INT(vpid), flags, &status)) < 0)
	rb_sys_fail(0);
    if (pid == 0) {
	rb_last_status = Qnil;
	return Qnil;
    }
    return INT2FIX(pid);
}

static VALUE
proc_waitpid2(argc, argv)
    int argc;
    VALUE *argv;
{
    VALUE pid = proc_waitpid(argc, argv);
    if (NIL_P(pid)) return Qnil;
    return rb_assoc_new(pid, rb_last_status);
}

#ifndef HAVE_STRING_H
char *strtok();
#endif

#ifdef HAVE_SETITIMER
#define before_exec() rb_thread_stop_timer()
#define after_exec() rb_thread_start_timer()
#else
#define before_exec()
#define after_exec()
#endif

extern char *dln_find_exe();

static void
security(str)
    char *str;
{
    if (rb_safe_level() > 0) {
	if (rb_env_path_tainted()) {
	    rb_raise(rb_eSecurityError, "Insecure PATH - %s", str);
	}
    }
}

static int
proc_exec_v(argv, prog)
    char **argv;
    char *prog;
{
    if (prog) {
	security(prog);
    }
    else {
	security(argv[0]);
	prog = dln_find_exe(argv[0], 0);
	if (!prog) {
	    errno = ENOENT;
	    return -1;
	}
    }
#if (defined(MSDOS) && !defined(DJGPP)) || defined(__human68k__) || defined(__EMX__) || defined(OS2)
    {
#if defined(__human68k__)
#define COMMAND "command.x"
#endif
#if defined(__EMX__) || defined(OS2) /* OS/2 emx */
#define COMMAND "cmd.exe"
#endif
#if (defined(MSDOS) && !defined(DJGPP))
#define COMMAND "command.com"
#endif
	char *extension;

	if ((extension = strrchr(prog, '.')) != NULL && strcasecmp(extension, ".bat") == 0) {
	    char **new_argv;
	    char *p;
	    int n;

	    for (n = 0; argv[n]; n++)
		/* no-op */;
	    new_argv = ALLOCA_N(char*, n + 2);
	    for (; n > 0; n--)
		new_argv[n + 1] = argv[n];
	    new_argv[1] = strcpy(ALLOCA_N(char, strlen(argv[0]) + 1), argv[0]);
	    for (p = new_argv[1]; *p != '\0'; p++)
		if (*p == '/')
		    *p = '\\';
	    new_argv[0] = COMMAND;
	    argv = new_argv;
	    prog = dln_find_exe(argv[0], 0);
	    if (!prog) {
		errno = ENOENT;
		return -1;
	    }
	}
    }
#endif /* MSDOS or __human68k__ or __EMX__ */
    before_exec();
    execv(prog, argv);
    after_exec();
    return -1;
}

static int
proc_exec_n(argc, argv, progv)
    int argc;
    VALUE *argv;
    VALUE progv;
{
    char *prog = 0;
    char **args;
    int i;

    if (progv) {
	prog = RSTRING(progv)->ptr;
    }
    args = ALLOCA_N(char*, argc+1);
    for (i=0; i<argc; i++) {
	args[i] = RSTRING(argv[i])->ptr;
    }
    args[i] = 0;
    if (args[0]) {
	return proc_exec_v(args, prog);
    }
    return -1;
}

int
rb_proc_exec(str)
    const char *str;
{
    const char *s = str;
    char *ss, *t;
    char **argv, **a;

    security(str);
    for (s=str; *s; s++) {
	if (*s != ' ' && !ISALPHA(*s) && strchr("*?{}[]<>()~&|\\$;'`\"\n",*s)) {
#if defined(MSDOS)
	    int state;
	    before_exec();
	    state = system(str);
	    after_exec();
	    if (state != -1)
		exit(state);
#else
#if defined(__human68k__) || defined(__CYGWIN32__) || defined(__EMX__)
	    char *shell = dln_find_exe("sh", 0);
	    int state = -1;
	    before_exec();
	    if (shell)
		execl(shell, "sh", "-c", str, (char *) NULL);
	    else
		state = system(str);
	    after_exec();
	    if (state != -1)
		exit(state);
#else
	    before_exec();
	    execl("/bin/sh", "sh", "-c", str, (char *)NULL);
	    after_exec();
#endif
#endif
	    return -1;
	}
    }
    a = argv = ALLOCA_N(char*, (s-str)/2+2);
    ss = ALLOCA_N(char, s-str+1);
    strcpy(ss, str);
    if (*a++ = strtok(ss, " \t")) {
	while (t = strtok(NULL, " \t")) {
	    *a++ = t;
	}
	*a = NULL;
    }
    if (argv[0]) {
	return proc_exec_v(argv, 0);
    }
    errno = ENOENT;
    return -1;
}

#if defined(__human68k__)
static int
proc_spawn_v(argv, prog)
    char **argv;
    char *prog;
{
    char *extension;
    int state;

    if (prog) {
	security(prog);
    }
    else {
	security(argv[0]);
	prog = dln_find_exe(argv[0], 0);
	if (!prog)
	    return -1;
    }

    if ((extension = strrchr(prog, '.')) != NULL && strcasecmp(extension, ".bat") == 0) {
	char **new_argv;
	char *p;
	int n;

	for (n = 0; argv[n]; n++)
	    /* no-op */;
	new_argv = ALLOCA_N(char*, n + 2);
	for (; n > 0; n--)
	    new_argv[n + 1] = argv[n];
	new_argv[1] = strcpy(ALLOCA_N(char, strlen(argv[0]) + 1), argv[0]);
	for (p = new_argv[1]; *p != '\0'; p++)
	    if (*p == '/')
		*p = '\\';
	new_argv[0] = COMMAND;
	argv = new_argv;
	prog = dln_find_exe(argv[0], 0);
	if (!prog) {
	    errno = ENOENT;
	    return -1;
	}
    }
    before_exec();
    state = spawnv(P_WAIT, prog, argv);
    after_exec();    
    return state;
}

static int
proc_spawn_n(argc, argv, prog)
    int argc;
    VALUE *argv;
    VALUE prog;
{
    char **args;
    int i;

    args = ALLOCA_N(char*, argc + 1);
    for (i = 0; i < argc; i++) {
	Check_SafeStr(argv[i]);
	args[i] = RSTRING(argv[i])->ptr;
    }
    Check_SafeStr(prog);
    args[i] = (char*) 0;
    if (args[0])
	return proc_spawn_v(args, RSTRING(prog)->ptr);
    return -1;
}

static int
proc_spawn(sv)
    VALUE sv;
{
    char *str;
    char *s, *t;
    char **argv, **a;
    int state;

    Check_SafeStr(sv);
    str = s = RSTRING(sv)->ptr;
    for (s = str; *s; s++) {
	if (*s != ' ' && !ISALPHA(*s) && strchr("*?{}[]<>()~&|\\$;'`\"\n",*s)) {
	    char *shell = dln_find_exe("sh", 0);
	    before_exec();
	    state = shell?spawnl(P_WAIT,shell,"sh","-c",str,(char*)NULL):system(str);
	    after_exec();
	    return state;
	}
    }
    a = argv = ALLOCA_N(char*, (s - str) / 2 + 2);
    s = ALLOCA_N(char, s - str + 1);
    strcpy(s, str);
    if (*a++ = strtok(s, " \t")) {
	while (t = strtok(NULL, " \t"))
	    *a++ = t;
	*a = NULL;
    }
    return argv[0] ? proc_spawn_v(argv, 0) : -1;
}
#endif /* __human68k__ */

static VALUE
rb_f_exec(argc, argv)
    int argc;
    VALUE *argv;
{
    VALUE prog = 0;
    int i;

    if (argc == 0) {
	rb_raise(rb_eArgError, "wrong # of arguments");
    }

    if (TYPE(argv[0]) == T_ARRAY) {
	if (RARRAY(argv[0])->len != 2) {
	    rb_raise(rb_eArgError, "wrong first argument");
	}
	prog = RARRAY(argv[0])->ptr[0];
	argv[0] = RARRAY(argv[0])->ptr[1];
    }
    if (prog) {
	Check_SafeStr(prog);
    }
    for (i = 0; i < argc; i++) {
	Check_SafeStr(argv[i]);
    }
    if (argc == 1 && prog == 0) {
	rb_proc_exec(RSTRING(argv[0])->ptr);
    }
    else {
	proc_exec_n(argc, argv, prog);
    }
    rb_sys_fail(RSTRING(argv[0])->ptr);
    return Qnil;		/* dummy */
}

static VALUE
rb_f_fork(obj)
    VALUE obj;
{
#if !defined(__human68k__) && !defined(NT) && !defined(__MACOS__) && !defined(__EMX__)
    int pid;

    rb_secure(2);
    switch (pid = fork()) {
      case 0:
#ifdef linux
	after_exec();
#endif
	rb_thread_atfork();
	if (rb_block_given_p()) {
	    int status;

	    rb_protect(rb_yield, Qnil, &status);
	    ruby_stop(status);
	}
	return Qnil;

      case -1:
	rb_sys_fail("fork(2)");
	return Qnil;

      default:
	return INT2FIX(pid);
    }
#else
    rb_notimplement();
#endif
}

static VALUE
rb_f_exit_bang(argc, argv, obj)
    int argc;
    VALUE *argv;
    VALUE obj;
{
    VALUE status;
    int istatus;

    rb_secure(4);
    if (rb_scan_args(argc, argv, "01", &status) == 1) {
	istatus = NUM2INT(status);
    }
    else {
	istatus = -1;
    }
    _exit(istatus);

    return Qnil;		/* not reached */
}

void
rb_syswait(pid)
    int pid;
{
    static int overriding;
    RETSIGTYPE (*hfunc)_((int)), (*qfunc)_((int)), (*ifunc)_((int));
    int status;
    int i, hooked = Qfalse;

    if (!overriding) {
#ifdef SIGHUP
	hfunc = signal(SIGHUP, SIG_IGN);
#endif
#ifdef SIGQUIT
	qfunc = signal(SIGQUIT, SIG_IGN);
#endif
	ifunc = signal(SIGINT, SIG_IGN);
	overriding = Qtrue;
	hooked = Qtrue;
    }

    do {
	i = rb_waitpid(pid, 0, &status);
    } while (i == -1 && errno == EINTR);

    if (hooked) {
#ifdef SIGHUP
	signal(SIGHUP, hfunc);
#endif
#ifdef SIGQUIT
	signal(SIGQUIT, qfunc);
#endif
	signal(SIGINT, ifunc);
	overriding = Qfalse;
    }
}

static VALUE
rb_f_system(argc, argv)
    int argc;
    VALUE *argv;
{
#if defined(NT) || defined(__EMX__)
    VALUE cmd;
    int state;

    fflush(stdout);
    fflush(stderr);
    if (argc == 0) {
	rb_last_status = INT2FIX(0);
	rb_raise(rb_eArgError, "wrong # of arguments");
    }

    if (TYPE(argv[0]) == T_ARRAY) {
	if (RARRAY(argv[0])->len != 2) {
	    rb_raise(rb_eArgError, "wrong first argument");
	}
	argv[0] = RARRAY(argv[0])->ptr[0];
    }
    cmd = rb_ary_join(rb_ary_new4(argc, argv), rb_str_new2(" "));

    Check_SafeStr(cmd);
    state = do_spawn(RSTRING(cmd)->ptr);
    rb_last_status = INT2FIX(state);

    if (state == 0) return Qtrue;
    return Qfalse;
#else
#ifdef DJGPP
    VALUE cmd;
    int state;

    if (argc == 0) {
	rb_last_status = INT2FIX(0);
	rb_raise(rb_eArgError, "wrong # of arguments");
    }

    if (TYPE(argv[0]) == T_ARRAY) {
	if (RARRAY(argv[0])->len != 2) {
	    rb_raise(rb_eArgError, "wrong first argument");
	}
	argv[0] = RARRAY(argv[0])->ptr[0];
    }
    cmd = rb_ary_join(rb_ary_new4(argc, argv), rb_str_new2(" "));

    Check_SafeStr(cmd);
    state = system(RSTRING(cmd)->ptr);
    rb_last_status = INT2FIX((state & 0xff) << 8);

    if (state == 0) return Qtrue;
    return Qfalse;
#else
#if defined(__human68k__)
    VALUE prog = 0;
    int i;
    int state;

    fflush(stdin);
    fflush(stdout);
    fflush(stderr);
    if (argc == 0) {
	rb_last_status = INT2FIX(0);
	rb_raise(rb_eArgError, "wrong # of arguments");
    }

    if (TYPE(argv[0]) == T_ARRAY) {
	if (RARRAY(argv[0])->len != 2) {
	    rb_raise(rb_eArgError, "wrong first argument");
	}
	prog = RARRAY(argv[0])->ptr[0];
	argv[0] = RARRAY(argv[0])->ptr[1];
    }

    if (argc == 1 && prog == 0) {
	state = proc_spawn(argv[0]);
    }
    else {
	state = proc_spawn_n(argc, argv, prog);
    }
    rb_last_status = state == -1 ? INT2FIX(127) : INT2FIX(state);
    return state == 0 ? Qtrue : Qfalse;
#else
    volatile VALUE prog = 0;
    int pid;
    int i;

    fflush(stdout);
    fflush(stderr);
    if (argc == 0) {
	rb_last_status = INT2FIX(0);
	rb_raise(rb_eArgError, "wrong # of arguments");
    }

    if (TYPE(argv[0]) == T_ARRAY) {
	if (RARRAY(argv[0])->len != 2) {
	    rb_raise(rb_eArgError, "wrong first argument");
	}
	prog = RARRAY(argv[0])->ptr[0];
	argv[0] = RARRAY(argv[0])->ptr[1];
    }

    if (prog) {
	Check_SafeStr(prog);
    }
    for (i = 0; i < argc; i++) {
	Check_SafeStr(argv[i]);
    }
  retry:
    switch (pid = fork()) {
      case 0:
	if (argc == 1 && prog == 0) {
	    rb_proc_exec(RSTRING(argv[0])->ptr);
	}
	else {
	    proc_exec_n(argc, argv, prog);
	}
	_exit(127);
	break;			/* not reached */

      case -1:
	if (errno == EAGAIN) {
	    rb_thread_sleep(1);
	    goto retry;
	}
	rb_sys_fail(0);
	break;

      default:
	rb_syswait(pid);
    }

    if (rb_last_status == INT2FIX(0)) return Qtrue;
    return Qfalse;
#endif /* __human68k__ */
#endif /* DJGPP */
#endif /* NT */
}

static VALUE
rb_f_sleep(argc, argv)
    int argc;
    VALUE *argv;
{
    int beg, end;

    beg = time(0);
    if (argc == 0) {
	rb_thread_sleep_forever();
    }
    else if (argc == 1) {
	rb_thread_wait_for(rb_time_interval(argv[0]));
    }
    else {
	rb_raise(rb_eArgError, "wrong # of arguments");
    }

    end = time(0) - beg;

    return INT2FIX(end);
}

static VALUE
proc_getpgrp(argc, argv)
    int argc;
    VALUE *argv;
{
#ifdef HAVE_GETPGRP
    int pgrp;
#ifndef GETPGRP_VOID
    VALUE vpid;
    int pid;

    rb_scan_args(argc, argv, "01", &vpid);
    pid = NIL_P(vpid)?0:NUM2INT(vpid);
    pgrp = getpgrp(pid);
#else
    rb_scan_args(argc, argv, "0");
    pgrp = getpgrp();
#endif
    if (pgrp < 0) rb_sys_fail(0);
    return INT2FIX(pgrp);
#else
    rb_notimplement();
#endif
}

static VALUE
proc_setpgrp(argc, argv)
    int argc;
    VALUE *argv;
{
#ifdef HAVE_SETPGRP
#ifndef SETPGRP_VOID
    VALUE pid, pgrp;
    int ipid, ipgrp;

    rb_scan_args(argc, argv, "02", &pid, &pgrp);

    ipid = NIL_P(pid)?0:NUM2INT(pid);
    ipgrp = NIL_P(pgrp)?0:NUM2INT(pgrp);
    if (setpgrp(ipid, ipgrp) < 0) rb_sys_fail(0);
#else
    rb_scan_args(argc, argv, "0");
    if (setpgrp() < 0) rb_sys_fail(0);
#endif
    return INT2FIX(0);
#else
    rb_notimplement();
#endif
}

static VALUE
proc_getpgid(obj, pid)
    VALUE obj, pid;
{
#if defined(HAVE_GETPGID) && !defined(__CHECKER__)
    int i;

    i = getpgid(NUM2INT(pid));
    return INT2NUM(i);
#else
    rb_notimplement();
#endif
}

static VALUE
proc_setpgid(obj, pid, pgrp)
    VALUE obj, pid, pgrp;
{
#ifdef HAVE_SETPGID
    int ipid, ipgrp;

    rb_secure(2);
    ipid = NUM2INT(pid);
    ipgrp = NUM2INT(pgrp);

    if (setpgid(ipid, ipgrp) < 0) rb_sys_fail(0);
    return INT2FIX(0);
#else
    rb_notimplement();
#endif
}

static VALUE
proc_setsid()
{
#if defined(HAVE_SETSID)
    int pid;

    rb_secure(2);
    pid = setsid();
    if (pid < 0) rb_sys_fail(0);
    return INT2FIX(pid);
#elif defined(HAVE_SETPGRP) && defined(TIOCNOTTY)
  pid_t pid;
  int ret;

  rb_secure(2);
  pid = getpid();
#if defined(SETPGRP_VOID)
  ret = setpgrp();
  /* If `pid_t setpgrp(void)' is equivalent to setsid(),
     `ret' will be the same value as `pid', and following open() will fail.
     In Linux, `int setpgrp(void)' is equivalent to setpgid(0, 0). */
#else
  ret = setpgrp(0, pid);
#endif
  if (ret == -1) rb_sys_fail(0);

  if ((fd = open("/dev/tty", O_RDWR)) >= 0) {
    ioctl(fd, TIOCNOTTY, NULL);
    close(fd);
  }
  return INT2FIX(pid);
#else
    rb_notimplement();
#endif
}

static VALUE
proc_getpriority(obj, which, who)
    VALUE obj, which, who;
{
#ifdef HAVE_GETPRIORITY
    int prio, iwhich, iwho;

    iwhich = NUM2INT(which);
    iwho   = NUM2INT(who);

    errno = 0;
    prio = getpriority(iwhich, iwho);
    if (errno) rb_sys_fail(0);
    return INT2FIX(prio);
#else
    rb_notimplement();
#endif
}

static VALUE
proc_setpriority(obj, which, who, prio)
    VALUE obj, which, who, prio;
{
#ifdef HAVE_GETPRIORITY
    int iwhich, iwho, iprio;

    rb_secure(2);
    iwhich = NUM2INT(which);
    iwho   = NUM2INT(who);
    iprio  = NUM2INT(prio);

    if (setpriority(iwhich, iwho, iprio) < 0)
	rb_sys_fail(0);
    return INT2FIX(0);
#else
    rb_notimplement();
#endif
}

static VALUE
proc_getuid(obj)
    VALUE obj;
{
    int uid = getuid();
    return INT2FIX(uid);
}

static VALUE
proc_setuid(obj, id)
    VALUE obj, id;
{
    int uid;

    uid = NUM2INT(id);
#ifdef HAVE_SETREUID
    if (setreuid(uid, -1) < 0) rb_sys_fail(0);
#else
#ifdef HAVE_SETRUID
    if (setruid(uid) < 0) rb_sys_fail(0);
#else
    {
	if (geteuid() == uid) {
	    if (setuid(uid) < 0) rb_sys_fail(0);
	}
	else {
	    rb_notimplement();
	}
    }
#endif
#endif
    return INT2FIX(uid);
}

static VALUE
proc_getgid(obj)
    VALUE obj;
{
    int gid = getgid();
    return INT2FIX(gid);
}

static VALUE
proc_setgid(obj, id)
    VALUE obj, id;
{
    int gid;

    gid = NUM2INT(id);
#ifdef HAVE_SETREGID
    if (setregid(gid, -1) < 0) rb_sys_fail(0);
#else
#ifdef HAS_SETRGID
    if (setrgid((GIDTYPE)gid) < 0) rb_sys_fail(0);
#else
    {
	if (getegid() == gid) {
	    if (setgid(gid) < 0) rb_sys_fail(0);
	}
	else {
	    rb_notimplement();
	}
    }
#endif
#endif
    return INT2FIX(gid);
}

static VALUE
proc_geteuid(obj)
    VALUE obj;
{
    int euid = geteuid();
    return INT2FIX(euid);
}

static VALUE
proc_seteuid(obj, euid)
    VALUE obj, euid;
{
#ifdef HAVE_SETREUID
    if (setreuid(-1, NUM2INT(euid)) < 0) rb_sys_fail(0);
#else
#ifdef HAVE_SETEUID
    if (seteuid(NUM2INT(euid)) < 0) rb_sys_fail(0);
#else
    euid = NUM2INT(euid);
    if (euid == getuid()) {
	if (setuid(euid) < 0) rb_sys_fail(0);
    }
    else {
	rb_notimplement();
    }
#endif
#endif
    return euid;
}

static VALUE
proc_getegid(obj)
    VALUE obj;
{
    int egid = getegid();
    return INT2FIX(egid);
}

static VALUE
proc_setegid(obj, egid)
    VALUE obj, egid;
{
    rb_secure(2);
#ifdef HAVE_SETREGID
    if (setregid(-1, NUM2INT(egid)) < 0) rb_sys_fail(0);
#else
#ifdef HAVE_SETEGID
    if (setegid(NUM2INT(egid)) < 0) rb_sys_fail(0);
#else
    egid = NUM2INT(egid);
    if (egid == getgid()) {
	if (setgid(egid) < 0) rb_sys_fail(0);
    }
    else {
	rb_notimplement();
    }
#endif
#endif
    return egid;
}

VALUE rb_mProcess;

void
Init_process()
{
    rb_define_virtual_variable("$$", get_pid, 0);
    rb_define_readonly_variable("$?", &rb_last_status);
    rb_define_global_function("exec", rb_f_exec, -1);
    rb_define_global_function("fork", rb_f_fork, 0);
    rb_define_global_function("exit!", rb_f_exit_bang, -1);
    rb_define_global_function("system", rb_f_system, -1);
    rb_define_global_function("sleep", rb_f_sleep, -1);

    rb_mProcess = rb_define_module("Process");

#if !defined(NT) && !defined(DJGPP)
#ifdef WNOHANG
    rb_define_const(rb_mProcess, "WNOHANG", INT2FIX(WNOHANG));
#else
    rb_define_const(rb_mProcess, "WNOHANG", INT2FIX(0));
#endif
#ifdef WUNTRACED
    rb_define_const(rb_mProcess, "WUNTRACED", INT2FIX(WUNTRACED));
#else
    rb_define_const(rb_mProcess, "WUNTRACED", INT2FIX(0));
#endif
#endif

    rb_define_singleton_method(rb_mProcess, "fork", rb_f_fork, 0);
    rb_define_singleton_method(rb_mProcess, "exit!", rb_f_exit_bang, -1);
    rb_define_module_function(rb_mProcess, "kill", rb_f_kill, -1);
#ifndef NT
    rb_define_module_function(rb_mProcess, "wait", proc_wait, 0);
    rb_define_module_function(rb_mProcess, "wait2", proc_wait2, 0);
    rb_define_module_function(rb_mProcess, "waitpid", proc_waitpid, -1);
    rb_define_module_function(rb_mProcess, "waitpid2", proc_waitpid2, -1);
#endif /* ifndef NT */

    rb_define_module_function(rb_mProcess, "pid", get_pid, 0);
    rb_define_module_function(rb_mProcess, "ppid", get_ppid, 0);

    rb_define_module_function(rb_mProcess, "getpgrp", proc_getpgrp, -1);
    rb_define_module_function(rb_mProcess, "setpgrp", proc_setpgrp, -1);
    rb_define_module_function(rb_mProcess, "getpgid", proc_getpgid, 1);
    rb_define_module_function(rb_mProcess, "setpgid", proc_setpgid, 2);

    rb_define_module_function(rb_mProcess, "setsid", proc_setsid, 0);

    rb_define_module_function(rb_mProcess, "getpriority", proc_getpriority, 2);
    rb_define_module_function(rb_mProcess, "setpriority", proc_setpriority, 3);

#ifdef HAVE_GETPRIORITY
    rb_define_const(rb_mProcess, "PRIO_PROCESS", INT2FIX(PRIO_PROCESS));
    rb_define_const(rb_mProcess, "PRIO_PGRP", INT2FIX(PRIO_PGRP));
    rb_define_const(rb_mProcess, "PRIO_USER", INT2FIX(PRIO_USER));
#endif

    rb_define_module_function(rb_mProcess, "uid", proc_getuid, 0);
    rb_define_module_function(rb_mProcess, "uid=", proc_setuid, 1);
    rb_define_module_function(rb_mProcess, "gid", proc_getgid, 0);
    rb_define_module_function(rb_mProcess, "gid=", proc_setgid, 1);
    rb_define_module_function(rb_mProcess, "euid", proc_geteuid, 0);
    rb_define_module_function(rb_mProcess, "euid=", proc_seteuid, 1);
    rb_define_module_function(rb_mProcess, "egid", proc_getegid, 0);
    rb_define_module_function(rb_mProcess, "egid=", proc_setegid, 1);
}
