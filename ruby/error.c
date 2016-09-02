/**********************************************************************

  error.c -

  $Author: melville $
  $Date: 2003/10/15 12:07:43 $
  created at: Mon Aug  9 16:11:34 JST 1993

  Copyright (C) 1993-2003 Yukihiro Matsumoto

**********************************************************************/

#include "ruby.h"
#include "env.h"
#include "version.h"
#include "st.h"

#include <stdio.h>
#ifdef HAVE_STDARG_PROTOTYPES
#include <stdarg.h>
#define va_init_list(a,b) va_start(a,b)
#else
#include <varargs.h>
#define va_init_list(a,b) va_start(a)
#endif

int ruby_nerrs;

static int
err_position(buf, len)
    char *buf;
    long len;
{
    ruby_set_current_source();
    if (!ruby_sourcefile) {
	return 0;
    }
    else if (ruby_sourceline == 0) {
	return snprintf(buf, len, "%s: ", ruby_sourcefile);
    }
    else {
	return snprintf(buf, len, "%s:%d: ", ruby_sourcefile, ruby_sourceline);
    }
}

static void
err_snprintf(buf, len, fmt, args)
    char *buf;
    long len;
    const char *fmt;
    va_list args;
{
    long n;

    n = err_position(buf, len);
    if (len > n) {
	vsnprintf((char*)buf+n, len-n, fmt, args);
    }
}

static void err_append _((const char*));
static void
err_print(fmt, args)
    const char *fmt;
    va_list args;
{
    char buf[BUFSIZ];

    err_snprintf(buf, BUFSIZ, fmt, args);
    err_append(buf);
}

void
#ifdef HAVE_STDARG_PROTOTYPES
rb_compile_error(const char *fmt, ...)
#else
rb_compile_error(fmt, va_alist)
    const char *fmt;
    va_dcl
#endif
{
    va_list args;

    va_init_list(args, fmt);
    err_print(fmt, args);
    va_end(args);
    ruby_nerrs++;
}

void
#ifdef HAVE_STDARG_PROTOTYPES
rb_compile_error_append(const char *fmt, ...)
#else
rb_compile_error_append(fmt, va_alist)
    const char *fmt;
    va_dcl
#endif
{
    va_list args;
    char buf[BUFSIZ];

    va_init_list(args, fmt);
    vsnprintf(buf, BUFSIZ, fmt, args);
    va_end(args);
    err_append(buf);
}

static void
warn_print(fmt, args)
    const char *fmt;
    va_list args;
{
    char buf[BUFSIZ];

    err_snprintf(buf, BUFSIZ, fmt, args);
    rb_write_error(buf);
    rb_write_error("\n");
}

void
#ifdef HAVE_STDARG_PROTOTYPES
rb_warn(const char *fmt, ...)
#else
rb_warn(fmt, va_alist)
    const char *fmt;
    va_dcl
#endif
{
    char buf[BUFSIZ];
    va_list args;

    if (NIL_P(ruby_verbose)) return;

    snprintf(buf, BUFSIZ, "warning: %s", fmt);

    va_init_list(args, fmt);
    warn_print(buf, args);
    va_end(args);
}

/* rb_warning() reports only in verbose mode */
void
#ifdef HAVE_STDARG_PROTOTYPES
rb_warning(const char *fmt, ...)
#else
rb_warning(fmt, va_alist)
    const char *fmt;
    va_dcl
#endif
{
    char buf[BUFSIZ];
    va_list args;

    if (!RTEST(ruby_verbose)) return;

    snprintf(buf, BUFSIZ, "warning: %s", fmt);

    va_init_list(args, fmt);
    warn_print(buf, args);
    va_end(args);
}

static VALUE
rb_warn_m(self, mesg)
    VALUE self, mesg;
{
    rb_io_write(rb_stderr, mesg);
    rb_io_write(rb_stderr, rb_default_rs);
    return mesg;
}

void
#ifdef HAVE_STDARG_PROTOTYPES
rb_bug(const char *fmt, ...)
#else
rb_bug(fmt, va_alist)
    const char *fmt;
    va_dcl
#endif
{
    char buf[BUFSIZ];
    va_list args;

    snprintf(buf, BUFSIZ, "[BUG] %s", fmt);
    ruby_in_eval = 0;

    va_init_list(args, fmt);
    warn_print(buf, args);
    va_end(args);
    snprintf(buf, BUFSIZ, "ruby %s (%s) [%s]\n", RUBY_VERSION, RUBY_RELEASE_DATE, RUBY_PLATFORM);
    rb_write_error(buf);
    rb_write_error("\n");
    abort();
}

static struct types {
    int type;
    const char *name;
} builtin_types[] = {
    {T_NIL,	"nil"},
    {T_OBJECT,	"Object"},
    {T_CLASS,	"Class"},
    {T_ICLASS,	"iClass"},	/* internal use: mixed-in module holder */
    {T_MODULE,	"Module"},
    {T_FLOAT,	"Float"},
    {T_STRING,	"String"},
    {T_REGEXP,	"Regexp"},
    {T_ARRAY,	"Array"},
    {T_FIXNUM,	"Fixnum"},
    {T_HASH,	"Hash"},
    {T_STRUCT,	"Struct"},
    {T_BIGNUM,	"Bignum"},
    {T_FILE,	"File"},
    {T_TRUE,	"true"},
    {T_FALSE,	"false"},
    {T_SYMBOL,	"Symbol"},	/* :symbol */
    {T_DATA,	"Data"},	/* internal use: wrapped C pointers */
    {T_MATCH,	"MatchData"},	/* data of $~ */
    {T_VARMAP,	"Varmap"},	/* internal use: dynamic variables */
    {T_SCOPE,	"Scope"},	/* internal use: variable scope */
    {T_NODE,	"Node"},	/* internal use: syntax tree node */
    {T_UNDEF,	"undef"},	/* internal use: #undef; should not happen */
    {-1,	0}
};

void
rb_check_type(x, t)
    VALUE x;
    int t;
{
    struct types *type = builtin_types;

    if (x == Qundef) {
	rb_bug("undef leaked to the Ruby space");
    }

    if (TYPE(x) != t) {
	while (type->type >= 0) {
	    if (type->type == t) {
		char *etype;

		if (NIL_P(x)) {
		    etype = "nil";
		}
		else if (FIXNUM_P(x)) {
		    etype = "Fixnum";
		}
		else if (rb_special_const_p(x)) {
		    etype = RSTRING(rb_obj_as_string(x))->ptr;
		}
		else {
		    etype = rb_obj_classname(x);
		}
		rb_raise(rb_eTypeError, "wrong argument type %s (expected %s)",
			 etype, type->name);
	    }
	    type++;
	}
	rb_bug("unknown type 0x%x", t);
    }
}

/* exception classes */
#include <errno.h>

VALUE rb_eException;
VALUE rb_eSystemExit;
VALUE rb_eInterrupt;
VALUE rb_eSignal;
VALUE rb_eFatal;
VALUE rb_eStandardError;
VALUE rb_eRuntimeError;
VALUE rb_eTypeError;
VALUE rb_eArgError;
VALUE rb_eIndexError;
VALUE rb_eRangeError;
VALUE rb_eNameError;
VALUE rb_eNoMethodError;
VALUE rb_eSecurityError;
VALUE rb_eNotImpError;
VALUE rb_eNoMemError;

VALUE rb_eScriptError;
VALUE rb_eSyntaxError;
VALUE rb_eLoadError;

VALUE rb_eSystemCallError;
VALUE rb_mErrno;

VALUE
rb_exc_new(etype, ptr, len)
    VALUE etype;
    const char *ptr;
    long len;
{
    return rb_funcall(etype, rb_intern("new"), 1, rb_str_new(ptr, len));
}

VALUE
rb_exc_new2(etype, s)
    VALUE etype;
    const char *s;
{
    return rb_exc_new(etype, s, strlen(s));
}

VALUE
rb_exc_new3(etype, str)
    VALUE etype, str;
{
    StringValue(str);
    return rb_exc_new(etype, RSTRING(str)->ptr, RSTRING(str)->len);
}

static VALUE
exc_initialize(argc, argv, exc)
    int argc;
    VALUE *argv;
    VALUE exc;
{
    VALUE mesg;

    if (rb_scan_args(argc, argv, "01", &mesg) == 1) {
	StringValue(mesg);	/* ensure mesg can be converted to String */
    }
    rb_iv_set(exc, "mesg", mesg);
    rb_iv_set(exc, "bt", Qnil);

    return exc;
}

static VALUE
exc_exception(argc, argv, self)
    int argc;
    VALUE *argv;
    VALUE self;
{
    VALUE exc;

    if (argc == 0) return self;
    if (argc == 1 && self == argv[0]) return self;
    exc = rb_obj_clone(self);
    exc_initialize(argc, argv, exc);

    return exc;
}

static VALUE
exc_to_s(exc)
    VALUE exc;
{
    VALUE mesg = rb_attr_get(exc, rb_intern("mesg"));

    if (NIL_P(mesg)) return rb_class_path(CLASS_OF(exc));
    if (OBJ_TAINTED(exc)) OBJ_TAINT(mesg);
    return mesg;
}

static VALUE
exc_inspect(exc)
    VALUE exc;
{
    VALUE str, klass;

    klass = CLASS_OF(exc);
    exc = rb_obj_as_string(exc);
    if (RSTRING(exc)->len == 0) {
	return rb_str_dup(rb_class_path(klass));
    }

    str = rb_str_buf_new2("#<");
    klass = rb_class_path(klass);
    rb_str_buf_append(str, klass);
    rb_str_buf_cat(str, ": ", 2);
    rb_str_buf_append(str, exc);
    rb_str_buf_cat(str, ">", 1);

    return str;
}

static VALUE
exc_backtrace(exc)
    VALUE exc;
{
    ID bt = rb_intern("bt");

    if (!rb_ivar_defined(exc, bt)) return Qnil;
    return rb_ivar_get(exc, bt);
}

static VALUE
check_backtrace(bt)
    VALUE bt;
{
    long i;
    static char *err = "backtrace must be Array of String";

    if (!NIL_P(bt)) {
	int t = TYPE(bt);

	if (t == T_STRING) return rb_ary_new3(1, bt);
	if (t != T_ARRAY) {
	    rb_raise(rb_eTypeError, err);
	}
	for (i=0;i<RARRAY(bt)->len;i++) {
	    if (TYPE(RARRAY(bt)->ptr[i]) != T_STRING) {
		rb_raise(rb_eTypeError, err);
	    }
	}
    }
    return bt;
}

static VALUE
exc_set_backtrace(exc, bt)
    VALUE exc;
    VALUE bt;
{
    return rb_iv_set(exc, "bt", check_backtrace(bt));
}

static VALUE
exit_initialize(argc, argv, exc)
    int argc;
    VALUE *argv;
    VALUE exc;
{
    VALUE status = INT2NUM(0);
    if (argc > 0 && FIXNUM_P(argv[0])) {
	status = *argv++;
	--argc;
    }
    exc_initialize(argc, argv, exc);
    rb_iv_set(exc, "status", status);
    return exc;
}

static VALUE
exit_status(exc)
    VALUE exc;
{
    return rb_attr_get(exc, rb_intern("status"));
}

void
#ifdef HAVE_STDARG_PROTOTYPES
rb_name_error(ID id, const char *fmt, ...)
#else
rb_name_error(id, fmt, va_alist)
    ID id;
    const char *fmt;
    va_dcl
#endif
{
    VALUE exc, argv[2];
    va_list args;
    char buf[BUFSIZ];

    va_init_list(args, fmt);
    vsnprintf(buf, BUFSIZ, fmt, args);
    va_end(args);

    argv[0] = rb_str_new2(buf);
    argv[1] = ID2SYM(id);
    exc = rb_class_new_instance(2, argv, rb_eNameError);
    rb_exc_raise(exc);
}

static VALUE
name_err_initialize(argc, argv, self)
    int argc;
    VALUE *argv;
    VALUE self;
{
    VALUE name = (argc > 1) ? argv[--argc] : Qnil;
    exc_initialize(argc, argv, self);
    rb_iv_set(self, "name", name);
    return self;
}

static VALUE
name_err_name(self)
    VALUE self;
{
    return rb_attr_get(self, rb_intern("name"));
}

static VALUE
nometh_err_initialize(argc, argv, self)
    int argc;
    VALUE *argv;
    VALUE self;
{
    VALUE args = (argc > 2) ? argv[--argc] : Qnil;
    name_err_initialize(argc, argv, self);
    rb_iv_set(self, "args", args);
    return self;
}

static VALUE
nometh_err_args(self)
    VALUE self;
{
    return rb_attr_get(self, rb_intern("args"));
}

void
rb_invalid_str(str, type)
    const char *str, *type;
{
    VALUE s = rb_str_inspect(rb_str_new2(str));

    rb_raise(rb_eArgError, "invalid value for %s: %s", type, RSTRING(s)->ptr);
}

static st_table *syserr_tbl;

static VALUE
set_syserr(n, name)
    int n;
    const char *name;
{
    VALUE error;

    if (!st_lookup(syserr_tbl, n, &error)) {
	error = rb_define_class_under(rb_mErrno, name, rb_eSystemCallError);
	rb_define_const(error, "Errno", INT2NUM(n));
	st_add_direct(syserr_tbl, n, error);
    }
    else {
	rb_define_const(rb_mErrno, name, error);
    }
    return error;
}

static VALUE
get_syserr(n)
    int n;
{
    VALUE error;

    if (!st_lookup(syserr_tbl, n, &error)) {
	char name[8];	/* some Windows' errno have 5 digits. */

	snprintf(name, sizeof(name), "E%03d", n);
	error = set_syserr(n, name);
    }
    return error;
}

static VALUE
syserr_initialize(argc, argv, self)
    int argc;
    VALUE *argv;
    VALUE self;
{
#if !defined(_WIN32) && !defined(__VMS)
    char *strerror();
#endif
    char *err;
    char *buf;
    VALUE mesg, error;
    VALUE klass = rb_obj_class(self);

    if (klass == rb_eSystemCallError) {
	rb_scan_args(argc, argv, "11", &mesg, &error);
	if (argc == 1 && FIXNUM_P(mesg)) {
	    error = mesg; mesg = Qnil;
	}
	if (!NIL_P(error) && st_lookup(syserr_tbl, NUM2LONG(error), &klass)) {
	    /* change class */
	    if (TYPE(self) != T_OBJECT) { /* insurance to avoid type crash */
		rb_raise(rb_eTypeError, "invalid instance type");
	    }
	    RBASIC(self)->klass = klass;
	}
    }
    else {
	rb_scan_args(argc, argv, "01", &mesg);
	error = rb_const_get_at(klass, rb_intern("Errno"));
    }
    if (!NIL_P(error)) err = strerror(NUM2LONG(error));
    else err = "unknown error";
    if (!NIL_P(mesg)) {
	StringValue(mesg);
	buf = ALLOCA_N(char, strlen(err)+RSTRING(mesg)->len+4);
	sprintf(buf, "%s - %.*s", err, (int)RSTRING(mesg)->len, RSTRING(mesg)->ptr);
	mesg = rb_str_new2(buf);
    }
    else {
	mesg = rb_str_new2(err);
    }
    exc_initialize(1, &mesg, self);
    rb_iv_set(self, "errno", error);
    return self;
}

static VALUE
syserr_errno(self)
    VALUE self;
{
    return rb_attr_get(self, rb_intern("errno"));
}

static VALUE
syserr_eqq(self, exc)
    VALUE self, exc;
{
    VALUE num;

    if (!rb_obj_is_kind_of(exc, rb_eSystemCallError)) return Qfalse;
    if (self == rb_eSystemCallError) return Qtrue;

    num = rb_attr_get(exc, rb_intern("errno"));
    if (NIL_P(num)) {
	VALUE klass = CLASS_OF(exc);

	while (TYPE(klass) == T_ICLASS || FL_TEST(klass, FL_SINGLETON)) {
	    klass = (VALUE)RCLASS(klass)->super;
	}
	num = rb_const_get(klass, rb_intern("Errno"));
    }
    if (rb_const_get(self, rb_intern("Errno")) == num)
	return Qtrue;
    return Qfalse;
}

static void init_syserr _((void));

void
Init_Exception()
{
    rb_eException   = rb_define_class("Exception", rb_cObject);
    rb_define_singleton_method(rb_eException, "exception", rb_class_new_instance, -1);
    rb_define_method(rb_eException, "exception", exc_exception, -1);
    rb_define_method(rb_eException, "initialize", exc_initialize, -1);
    rb_define_method(rb_eException, "to_s", exc_to_s, 0);
    rb_define_method(rb_eException, "to_str", exc_to_s, 0);
    rb_define_method(rb_eException, "message", exc_to_s, 0);
    rb_define_method(rb_eException, "inspect", exc_inspect, 0);
    rb_define_method(rb_eException, "backtrace", exc_backtrace, 0);
    rb_define_method(rb_eException, "set_backtrace", exc_set_backtrace, 1);

    rb_eSystemExit  = rb_define_class("SystemExit", rb_eException);
    rb_define_method(rb_eSystemExit, "initialize", exit_initialize, -1);
    rb_define_method(rb_eSystemExit, "status", exit_status, 0);

    rb_eFatal  	    = rb_define_class("fatal", rb_eException);
    rb_eSignal      = rb_define_class("SignalException", rb_eException);
    rb_eInterrupt   = rb_define_class("Interrupt", rb_eSignal);

    rb_eStandardError = rb_define_class("StandardError", rb_eException);
    rb_eTypeError     = rb_define_class("TypeError", rb_eStandardError);
    rb_eArgError      = rb_define_class("ArgumentError", rb_eStandardError);
    rb_eIndexError    = rb_define_class("IndexError", rb_eStandardError);
    rb_eRangeError    = rb_define_class("RangeError", rb_eStandardError);
    rb_eNameError     = rb_define_class("NameError", rb_eStandardError);
    rb_define_method(rb_eNameError, "initialize", name_err_initialize, -1);
    rb_define_method(rb_eNameError, "name", name_err_name, 0);
    rb_eNoMethodError = rb_define_class("NoMethodError", rb_eNameError);
    rb_define_method(rb_eNoMethodError, "initialize", nometh_err_initialize, -1);
    rb_define_method(rb_eNoMethodError, "args", nometh_err_args, 0);

    rb_eScriptError = rb_define_class("ScriptError", rb_eException);
    rb_eSyntaxError = rb_define_class("SyntaxError", rb_eScriptError);
    rb_eLoadError   = rb_define_class("LoadError", rb_eScriptError);
    rb_eNotImpError = rb_define_class("NotImplementedError", rb_eScriptError);

    rb_eRuntimeError = rb_define_class("RuntimeError", rb_eStandardError);
    rb_eSecurityError = rb_define_class("SecurityError", rb_eStandardError);
    rb_eNoMemError = rb_define_class("NoMemoryError", rb_eException);

    init_syserr();

    rb_define_global_function("warn", rb_warn_m, 1);
}

void
#ifdef HAVE_STDARG_PROTOTYPES
rb_raise(VALUE exc, const char *fmt, ...)
#else
rb_raise(exc, fmt, va_alist)
    VALUE exc;
    const char *fmt;
    va_dcl
#endif
{
    va_list args;
    char buf[BUFSIZ];

    va_init_list(args,fmt);
    vsnprintf(buf, BUFSIZ, fmt, args);
    va_end(args);
    rb_exc_raise(rb_exc_new2(exc, buf));
}

void
#ifdef HAVE_STDARG_PROTOTYPES
rb_loaderror(const char *fmt, ...)
#else
rb_loaderror(fmt, va_alist)
    const char *fmt;
    va_dcl
#endif
{
    va_list args;
    char buf[BUFSIZ];

    va_init_list(args, fmt);
    vsnprintf(buf, BUFSIZ, fmt, args);
    va_end(args);
    rb_exc_raise(rb_exc_new2(rb_eLoadError, buf));
}

void
rb_notimplement()
{
    rb_raise(rb_eNotImpError,
	     "The %s() function is unimplemented on this machine",
	     rb_id2name(ruby_frame->last_func));
}

void
#ifdef HAVE_STDARG_PROTOTYPES
rb_fatal(const char *fmt, ...)
#else
rb_fatal(fmt, va_alist)
    const char *fmt;
    va_dcl
#endif
{
    va_list args;
    char buf[BUFSIZ];

    va_init_list(args, fmt);
    vsnprintf(buf, BUFSIZ, fmt, args);
    va_end(args);

    ruby_in_eval = 0;
    rb_exc_fatal(rb_exc_new2(rb_eFatal, buf));
}

void
rb_sys_fail(mesg)
    const char *mesg;
{
    extern int errno;
    int n = errno;
    VALUE arg;

    errno = 0;
    if (n == 0) {
	rb_bug("rb_sys_fail() - errno == 0");
    }

    arg = mesg ? rb_str_new2(mesg) : Qnil;
    rb_exc_raise(rb_class_new_instance(1, &arg, get_syserr(n)));
}

void
#ifdef HAVE_STDARG_PROTOTYPES
rb_sys_warning(const char *fmt, ...)
#else
rb_sys_warning(fmt, va_alist)
     const char *fmt;
     va_dcl
#endif
{
     char buf[BUFSIZ];
     va_list args;
     int errno_save;
     
     errno_save = errno;

     if (!RTEST(ruby_verbose)) return;

     snprintf(buf, BUFSIZ, "warning: %s", fmt);
     snprintf(buf+strlen(buf), BUFSIZ-strlen(buf), ": %s", strerror(errno_save));
     
     va_init_list(args, fmt);
     warn_print(buf, args);
     va_end(args);
     errno = errno_save;
}

void
rb_load_fail(path)
    char *path;
{
    rb_loaderror("%s -- %s", strerror(errno), path);
}

void
rb_error_frozen(what)
    char *what;
{
    rb_raise(rb_eTypeError, "can't modify frozen %s", what);
}

void
rb_check_frozen(obj)
    VALUE obj;
{
    if (OBJ_FROZEN(obj)) rb_error_frozen(rb_obj_classname(obj));
}

static void
init_syserr()
{
    syserr_tbl = st_init_numtable();
    rb_eSystemCallError = rb_define_class("SystemCallError", rb_eStandardError);
    rb_define_method(rb_eSystemCallError, "initialize", syserr_initialize, -1);
    rb_define_method(rb_eSystemCallError, "errno", syserr_errno, 0);
    rb_define_singleton_method(rb_eSystemCallError, "===", syserr_eqq, 1);

    rb_mErrno = rb_define_module("Errno");
#ifdef EPERM
    set_syserr(EPERM, "EPERM");
#endif
#ifdef ENOENT
    set_syserr(ENOENT, "ENOENT");
#endif
#ifdef ESRCH
    set_syserr(ESRCH, "ESRCH");
#endif
#ifdef EINTR
    set_syserr(EINTR, "EINTR");
#endif
#ifdef EIO
    set_syserr(EIO, "EIO");
#endif
#ifdef ENXIO
    set_syserr(ENXIO, "ENXIO");
#endif
#ifdef E2BIG
    set_syserr(E2BIG, "E2BIG");
#endif
#ifdef ENOEXEC
    set_syserr(ENOEXEC, "ENOEXEC");
#endif
#ifdef EBADF
    set_syserr(EBADF, "EBADF");
#endif
#ifdef ECHILD
    set_syserr(ECHILD, "ECHILD");
#endif
#ifdef EAGAIN
    set_syserr(EAGAIN, "EAGAIN");
#endif
#ifdef ENOMEM
    set_syserr(ENOMEM, "ENOMEM");
#endif
#ifdef EACCES
    set_syserr(EACCES, "EACCES");
#endif
#ifdef EFAULT
    set_syserr(EFAULT, "EFAULT");
#endif
#ifdef ENOTBLK
    set_syserr(ENOTBLK, "ENOTBLK");
#endif
#ifdef EBUSY
    set_syserr(EBUSY, "EBUSY");
#endif
#ifdef EEXIST
    set_syserr(EEXIST, "EEXIST");
#endif
#ifdef EXDEV
    set_syserr(EXDEV, "EXDEV");
#endif
#ifdef ENODEV
    set_syserr(ENODEV, "ENODEV");
#endif
#ifdef ENOTDIR
    set_syserr(ENOTDIR, "ENOTDIR");
#endif
#ifdef EISDIR
    set_syserr(EISDIR, "EISDIR");
#endif
#ifdef EINVAL
    set_syserr(EINVAL, "EINVAL");
#endif
#ifdef ENFILE
    set_syserr(ENFILE, "ENFILE");
#endif
#ifdef EMFILE
    set_syserr(EMFILE, "EMFILE");
#endif
#ifdef ENOTTY
    set_syserr(ENOTTY, "ENOTTY");
#endif
#ifdef ETXTBSY
    set_syserr(ETXTBSY, "ETXTBSY");
#endif
#ifdef EFBIG
    set_syserr(EFBIG, "EFBIG");
#endif
#ifdef ENOSPC
    set_syserr(ENOSPC, "ENOSPC");
#endif
#ifdef ESPIPE
    set_syserr(ESPIPE, "ESPIPE");
#endif
#ifdef EROFS
    set_syserr(EROFS, "EROFS");
#endif
#ifdef EMLINK
    set_syserr(EMLINK, "EMLINK");
#endif
#ifdef EPIPE
    set_syserr(EPIPE, "EPIPE");
#endif
#ifdef EDOM
    set_syserr(EDOM, "EDOM");
#endif
#ifdef ERANGE
    set_syserr(ERANGE, "ERANGE");
#endif
#ifdef EDEADLK
    set_syserr(EDEADLK, "EDEADLK");
#endif
#ifdef ENAMETOOLONG
    set_syserr(ENAMETOOLONG, "ENAMETOOLONG");
#endif
#ifdef ENOLCK
    set_syserr(ENOLCK, "ENOLCK");
#endif
#ifdef ENOSYS
    set_syserr(ENOSYS, "ENOSYS");
#endif
#ifdef ENOTEMPTY
    set_syserr(ENOTEMPTY, "ENOTEMPTY");
#endif
#ifdef ELOOP
    set_syserr(ELOOP, "ELOOP");
#endif
#ifdef EWOULDBLOCK
    set_syserr(EWOULDBLOCK, "EWOULDBLOCK");
#endif
#ifdef ENOMSG
    set_syserr(ENOMSG, "ENOMSG");
#endif
#ifdef EIDRM
    set_syserr(EIDRM, "EIDRM");
#endif
#ifdef ECHRNG
    set_syserr(ECHRNG, "ECHRNG");
#endif
#ifdef EL2NSYNC
    set_syserr(EL2NSYNC, "EL2NSYNC");
#endif
#ifdef EL3HLT
    set_syserr(EL3HLT, "EL3HLT");
#endif
#ifdef EL3RST
    set_syserr(EL3RST, "EL3RST");
#endif
#ifdef ELNRNG
    set_syserr(ELNRNG, "ELNRNG");
#endif
#ifdef EUNATCH
    set_syserr(EUNATCH, "EUNATCH");
#endif
#ifdef ENOCSI
    set_syserr(ENOCSI, "ENOCSI");
#endif
#ifdef EL2HLT
    set_syserr(EL2HLT, "EL2HLT");
#endif
#ifdef EBADE
    set_syserr(EBADE, "EBADE");
#endif
#ifdef EBADR
    set_syserr(EBADR, "EBADR");
#endif
#ifdef EXFULL
    set_syserr(EXFULL, "EXFULL");
#endif
#ifdef ENOANO
    set_syserr(ENOANO, "ENOANO");
#endif
#ifdef EBADRQC
    set_syserr(EBADRQC, "EBADRQC");
#endif
#ifdef EBADSLT
    set_syserr(EBADSLT, "EBADSLT");
#endif
#ifdef EDEADLOCK
    set_syserr(EDEADLOCK, "EDEADLOCK");
#endif
#ifdef EBFONT
    set_syserr(EBFONT, "EBFONT");
#endif
#ifdef ENOSTR
    set_syserr(ENOSTR, "ENOSTR");
#endif
#ifdef ENODATA
    set_syserr(ENODATA, "ENODATA");
#endif
#ifdef ETIME
    set_syserr(ETIME, "ETIME");
#endif
#ifdef ENOSR
    set_syserr(ENOSR, "ENOSR");
#endif
#ifdef ENONET
    set_syserr(ENONET, "ENONET");
#endif
#ifdef ENOPKG
    set_syserr(ENOPKG, "ENOPKG");
#endif
#ifdef EREMOTE
    set_syserr(EREMOTE, "EREMOTE");
#endif
#ifdef ENOLINK
    set_syserr(ENOLINK, "ENOLINK");
#endif
#ifdef EADV
    set_syserr(EADV, "EADV");
#endif
#ifdef ESRMNT
    set_syserr(ESRMNT, "ESRMNT");
#endif
#ifdef ECOMM
    set_syserr(ECOMM, "ECOMM");
#endif
#ifdef EPROTO
    set_syserr(EPROTO, "EPROTO");
#endif
#ifdef EMULTIHOP
    set_syserr(EMULTIHOP, "EMULTIHOP");
#endif
#ifdef EDOTDOT
    set_syserr(EDOTDOT, "EDOTDOT");
#endif
#ifdef EBADMSG
    set_syserr(EBADMSG, "EBADMSG");
#endif
#ifdef EOVERFLOW
    set_syserr(EOVERFLOW, "EOVERFLOW");
#endif
#ifdef ENOTUNIQ
    set_syserr(ENOTUNIQ, "ENOTUNIQ");
#endif
#ifdef EBADFD
    set_syserr(EBADFD, "EBADFD");
#endif
#ifdef EREMCHG
    set_syserr(EREMCHG, "EREMCHG");
#endif
#ifdef ELIBACC
    set_syserr(ELIBACC, "ELIBACC");
#endif
#ifdef ELIBBAD
    set_syserr(ELIBBAD, "ELIBBAD");
#endif
#ifdef ELIBSCN
    set_syserr(ELIBSCN, "ELIBSCN");
#endif
#ifdef ELIBMAX
    set_syserr(ELIBMAX, "ELIBMAX");
#endif
#ifdef ELIBEXEC
    set_syserr(ELIBEXEC, "ELIBEXEC");
#endif
#ifdef EILSEQ
    set_syserr(EILSEQ, "EILSEQ");
#endif
#ifdef ERESTART
    set_syserr(ERESTART, "ERESTART");
#endif
#ifdef ESTRPIPE
    set_syserr(ESTRPIPE, "ESTRPIPE");
#endif
#ifdef EUSERS
    set_syserr(EUSERS, "EUSERS");
#endif
#ifdef ENOTSOCK
    set_syserr(ENOTSOCK, "ENOTSOCK");
#endif
#ifdef EDESTADDRREQ
    set_syserr(EDESTADDRREQ, "EDESTADDRREQ");
#endif
#ifdef EMSGSIZE
    set_syserr(EMSGSIZE, "EMSGSIZE");
#endif
#ifdef EPROTOTYPE
    set_syserr(EPROTOTYPE, "EPROTOTYPE");
#endif
#ifdef ENOPROTOOPT
    set_syserr(ENOPROTOOPT, "ENOPROTOOPT");
#endif
#ifdef EPROTONOSUPPORT
    set_syserr(EPROTONOSUPPORT, "EPROTONOSUPPORT");
#endif
#ifdef ESOCKTNOSUPPORT
    set_syserr(ESOCKTNOSUPPORT, "ESOCKTNOSUPPORT");
#endif
#ifdef EOPNOTSUPP
    set_syserr(EOPNOTSUPP, "EOPNOTSUPP");
#endif
#ifdef EPFNOSUPPORT
    set_syserr(EPFNOSUPPORT, "EPFNOSUPPORT");
#endif
#ifdef EAFNOSUPPORT
    set_syserr(EAFNOSUPPORT, "EAFNOSUPPORT");
#endif
#ifdef EADDRINUSE
    set_syserr(EADDRINUSE, "EADDRINUSE");
#endif
#ifdef EADDRNOTAVAIL
    set_syserr(EADDRNOTAVAIL, "EADDRNOTAVAIL");
#endif
#ifdef ENETDOWN
    set_syserr(ENETDOWN, "ENETDOWN");
#endif
#ifdef ENETUNREACH
    set_syserr(ENETUNREACH, "ENETUNREACH");
#endif
#ifdef ENETRESET
    set_syserr(ENETRESET, "ENETRESET");
#endif
#ifdef ECONNABORTED
    set_syserr(ECONNABORTED, "ECONNABORTED");
#endif
#ifdef ECONNRESET
    set_syserr(ECONNRESET, "ECONNRESET");
#endif
#ifdef ENOBUFS
    set_syserr(ENOBUFS, "ENOBUFS");
#endif
#ifdef EISCONN
    set_syserr(EISCONN, "EISCONN");
#endif
#ifdef ENOTCONN
    set_syserr(ENOTCONN, "ENOTCONN");
#endif
#ifdef ESHUTDOWN
    set_syserr(ESHUTDOWN, "ESHUTDOWN");
#endif
#ifdef ETOOMANYREFS
    set_syserr(ETOOMANYREFS, "ETOOMANYREFS");
#endif
#ifdef ETIMEDOUT
    set_syserr(ETIMEDOUT, "ETIMEDOUT");
#endif
#ifdef ECONNREFUSED
    set_syserr(ECONNREFUSED, "ECONNREFUSED");
#endif
#ifdef EHOSTDOWN
    set_syserr(EHOSTDOWN, "EHOSTDOWN");
#endif
#ifdef EHOSTUNREACH
    set_syserr(EHOSTUNREACH, "EHOSTUNREACH");
#endif
#ifdef EALREADY
    set_syserr(EALREADY, "EALREADY");
#endif
#ifdef EINPROGRESS
    set_syserr(EINPROGRESS, "EINPROGRESS");
#endif
#ifdef ESTALE
    set_syserr(ESTALE, "ESTALE");
#endif
#ifdef EUCLEAN
    set_syserr(EUCLEAN, "EUCLEAN");
#endif
#ifdef ENOTNAM
    set_syserr(ENOTNAM, "ENOTNAM");
#endif
#ifdef ENAVAIL
    set_syserr(ENAVAIL, "ENAVAIL");
#endif
#ifdef EISNAM
    set_syserr(EISNAM, "EISNAM");
#endif
#ifdef EREMOTEIO
    set_syserr(EREMOTEIO, "EREMOTEIO");
#endif
#ifdef EDQUOT
    set_syserr(EDQUOT, "EDQUOT");
#endif
}

static void
err_append(s)
    const char *s;
{
    extern VALUE ruby_errinfo;

    if (ruby_in_eval) {
	if (NIL_P(ruby_errinfo)) {
	    ruby_errinfo = rb_exc_new2(rb_eSyntaxError, s);
	}
	else {
	    VALUE str = rb_obj_as_string(ruby_errinfo);

	    rb_str_cat2(str, "\n");
	    rb_str_cat2(str, s);
	    ruby_errinfo = rb_exc_new3(rb_eSyntaxError, str);
	}
    }
    else {
	rb_write_error(s);
	rb_write_error("\n");
    }
}
