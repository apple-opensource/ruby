/**********************************************************************

  eval.c -

  $Author: melville $
  $Date: 2003/10/15 12:08:36 $
  created at: Thu Jun 10 14:22:17 JST 1993

  Copyright (C) 1993-2003 Yukihiro Matsumoto
  Copyright (C) 2000  Network Applied Communication Laboratory, Inc.
  Copyright (C) 2000  Information-technology Promotion Agency, Japan

**********************************************************************/

#include "ruby.h"
#include "node.h"
#include "env.h"
#include "util.h"
#include "rubysig.h"

#include <stdio.h>
#include <setjmp.h>
#include "st.h"
#include "dln.h"

#ifdef __APPLE__
#include <crt_externs.h>
#endif

/* Make alloca work the best possible way.  */
#ifdef __GNUC__
# ifndef atarist
#  ifndef alloca
#   define alloca __builtin_alloca
#  endif
# endif /* atarist */
#else
# ifdef HAVE_ALLOCA_H
#  include <alloca.h>
# else
#  ifdef _AIX
 #pragma alloca
#  else
#   ifndef alloca /* predefined by HP cc +Olibcalls */
void *alloca ();
#   endif
#  endif /* AIX */
# endif /* HAVE_ALLOCA_H */
#endif /* __GNUC__ */

#ifdef HAVE_STDARG_PROTOTYPES
#include <stdarg.h>
#define va_init_list(a,b) va_start(a,b)
#else
#include <varargs.h>
#define va_init_list(a,b) va_start(a)
#endif

#ifndef HAVE_STRING_H
char *strrchr _((const char*,const char));
#endif

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#ifdef __BEOS__
#include <net/socket.h>
#endif

#ifdef __MACOS__
#include "macruby_private.h"
#endif

#ifndef setjmp
#ifdef HAVE__SETJMP
#define setjmp(env) _setjmp(env)
#define longjmp(env,val) _longjmp(env,val)
#endif
#endif

#include <sys/types.h>
#include <signal.h>
#include <errno.h>

#if defined(__VMS)
#pragma nostandard
#endif

#ifdef HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif

#include <sys/stat.h>

VALUE rb_cProc;
static VALUE rb_cBinding;
static VALUE proc_invoke _((VALUE,VALUE,VALUE,VALUE));
static VALUE rb_f_binding _((VALUE));
static void rb_f_END _((void));
static VALUE rb_f_block_given_p _((void));
static VALUE block_pass _((VALUE,NODE*));
static VALUE rb_cMethod;
static VALUE method_call _((int, VALUE*, VALUE));
static VALUE rb_cUnboundMethod;
static VALUE umethod_bind _((VALUE, VALUE));
static VALUE rb_mod_define_method _((int, VALUE*, VALUE));

static int scope_vmode;
#define SCOPE_PUBLIC    0
#define SCOPE_PRIVATE   1
#define SCOPE_PROTECTED 2
#define SCOPE_MODFUNC   5
#define SCOPE_MASK      7
#define SCOPE_SET(f)  (scope_vmode=(f))
#define SCOPE_TEST(f) (scope_vmode&(f))

NODE* ruby_current_node;
int ruby_safe_level = 0;
/* safe-level:
   0 - strings from streams/environment/ARGV are tainted (default)
   1 - no dangerous operation by tainted value
   2 - process/file operations prohibited
   3 - all genetated objects are tainted
   4 - no global (non-tainted) variable modification/no direct output
*/

static VALUE safe_getter _((void));
static void safe_setter _((VALUE val));

void
rb_secure(level)
    int level;
{
    if (level <= ruby_safe_level) {
	rb_raise(rb_eSecurityError, "Insecure operation `%s' at level %d",
		 rb_id2name(ruby_frame->last_func), ruby_safe_level);
    }
}

void
rb_secure_update(obj)
    VALUE obj;
{
    if (!OBJ_TAINTED(obj)) rb_secure(4);
}

void
rb_check_safe_obj(x)
    VALUE x;
{
    if (ruby_safe_level > 0 && OBJ_TAINTED(x)){
	if (ruby_frame->last_func) {
	    rb_raise(rb_eSecurityError, "Insecure operation - %s",
		     rb_id2name(ruby_frame->last_func));
	}
	else {
	    rb_raise(rb_eSecurityError, "Insecure operation: -r");
	}
    }
    rb_secure(4);
}

void
rb_check_safe_str(x)
    VALUE x;
{
    rb_check_safe_obj(x);
    if (TYPE(x)!= T_STRING) {
	rb_raise(rb_eTypeError, "wrong argument type %s (expected String)",
		 rb_obj_classname(x));
    }
}

NORETURN(static void print_undef _((VALUE, ID)));
static void
print_undef(klass, id)
    VALUE klass;
    ID id;
{
    rb_name_error(id, "undefined method `%s' for %s `%s'",
		  rb_id2name(id), 
		  (TYPE(klass) == T_MODULE) ? "module" : "class",
		  rb_class2name(klass));
}

static ID removed, singleton_removed, undefined, singleton_undefined;

#define CACHE_SIZE 0x800
#define CACHE_MASK 0x7ff
#define EXPR1(c,m) ((((c)>>3)^(m))&CACHE_MASK)

struct cache_entry {		/* method hash table. */
    ID mid;			/* method's id */
    ID mid0;			/* method's original id */
    VALUE klass;		/* receiver's class */
    VALUE origin;		/* where method defined  */
    NODE *method;
    int noex;
};

static struct cache_entry cache[CACHE_SIZE];
static int ruby_running = 0;

void
rb_clear_cache()
{
   struct cache_entry *ent, *end;

    if (!ruby_running) return;
    ent = cache; end = ent + CACHE_SIZE;
    while (ent < end) {
	ent->mid = 0;
	ent++;
    }
}

static void
rb_clear_cache_for_undef(klass, id)
    ID id;
{
    struct cache_entry *ent, *end;

    if (!ruby_running) return;
    ent = cache; end = ent + CACHE_SIZE;
    while (ent < end) {
	if (ent->origin == klass && ent->mid == id) {
	    ent->mid = 0;
	}
	ent++;
    }
}

static void
rb_clear_cache_by_id(id)
    ID id;
{
    struct cache_entry *ent, *end;

    if (!ruby_running) return;
    ent = cache; end = ent + CACHE_SIZE;
    while (ent < end) {
	if (ent->mid == id) {
	    ent->mid = 0;
	}
	ent++;
    }
}

void
rb_clear_cache_by_class(klass)
    VALUE klass;
{
    struct cache_entry *ent, *end;

    if (!ruby_running) return;
    ent = cache; end = ent + CACHE_SIZE;
    while (ent < end) {
	if (ent->klass == klass || ent->origin == klass) {
	    ent->mid = 0;
	}
	ent++;
    }
}

static ID init, eqq, each, aref, aset, match, missing;
static ID added, singleton_added;
static ID __id__, __send__;

void
rb_add_method(klass, mid, node, noex)
    VALUE klass;
    ID mid;
    NODE *node;
    int noex;
{
    NODE *body;

    if (NIL_P(klass)) klass = rb_cObject;
    if (ruby_safe_level >= 4 && (klass == rb_cObject || !OBJ_TAINTED(klass))) {
	rb_raise(rb_eSecurityError, "Insecure: can't define method");
    }
    if (!FL_TEST(klass, FL_SINGLETON) &&
	node && nd_type(node) != NODE_ZSUPER &&
	(mid == rb_intern("initialize" )|| mid == rb_intern("initialize_copy"))) {
	noex = NOEX_PRIVATE | noex;
    }
    else if (FL_TEST(klass, FL_SINGLETON) && node && nd_type(node) == NODE_CFUNC &&
	     mid == rb_intern("allocate")) {
	rb_warn("defining %s.allocate is deprecated; use rb_define_alloc_func()",
		rb_class2name(rb_iv_get(klass, "__attached__")));
	mid = ID_ALLOCATOR;
    }
    if (OBJ_FROZEN(klass)) rb_error_frozen("class/module");
    rb_clear_cache_by_id(mid);
    body = NEW_METHOD(node, noex);
    st_insert(RCLASS(klass)->m_tbl, mid, (st_data_t)body);
    if (node && mid != ID_ALLOCATOR && ruby_running) {
	if (FL_TEST(klass, FL_SINGLETON)) {
	    rb_funcall(rb_iv_get(klass, "__attached__"), singleton_added, 1, ID2SYM(mid));
	}
	else {
	    rb_funcall(klass, added, 1, ID2SYM(mid));
	}
    }
}

void
rb_define_alloc_func(klass, func)
    VALUE klass;
    VALUE (*func) _((VALUE));
{
    Check_Type(klass, T_CLASS);
    rb_add_method(CLASS_OF(klass), ID_ALLOCATOR, NEW_CFUNC(func, 0), NOEX_PRIVATE);
}

void
rb_undef_alloc_func(klass)
    VALUE klass;
{
    Check_Type(klass, T_CLASS);
    rb_add_method(CLASS_OF(klass), ID_ALLOCATOR, 0, NOEX_UNDEF);
}

static NODE*
search_method(klass, id, origin)
    VALUE klass, *origin;
    ID id;
{
    NODE *body;

    if (!klass) return 0;
    while (!st_lookup(RCLASS(klass)->m_tbl, id, (st_data_t *)&body)) {
	klass = RCLASS(klass)->super;
	if (!klass) return 0;
    }

    if (origin) *origin = klass;
    return body;
}

static NODE*
rb_get_method_body(klassp, idp, noexp)
    VALUE *klassp;
    ID *idp;
    int *noexp;
{
    ID id = *idp;
    VALUE klass = *klassp;
    VALUE origin;
    NODE * volatile body;
    struct cache_entry *ent;

    if ((body = search_method(klass, id, &origin)) == 0 || !body->nd_body) {
	/* store empty info in cache */
	ent = cache + EXPR1(klass, id);
	ent->klass  = klass;
	ent->origin = klass;
	ent->mid = ent->mid0 = id;
	ent->noex   = 0;
	ent->method = 0;
	
	return 0;
    }

    if (ruby_running) {
	/* store in cache */
	if (BUILTIN_TYPE(origin) == T_ICLASS) origin = RBASIC(origin)->klass;
	ent = cache + EXPR1(klass, id);
	ent->klass  = klass;
	ent->noex   = body->nd_noex;
	if (noexp) *noexp = body->nd_noex;
	body = body->nd_body;
	if (nd_type(body) == NODE_FBODY) {
	    ent->mid = id;
	    *klassp = body->nd_orig;
	    ent->origin = body->nd_orig;
	    *idp = ent->mid0 = body->nd_mid;
	    body = ent->method = body->nd_head;
	}
	else {
	    *klassp = origin;
	    ent->origin = origin;
	    ent->mid = ent->mid0 = id;
	    ent->method = body;
	}
    }
    else {
	if (noexp) *noexp = body->nd_noex;
	body = body->nd_body;
	if (nd_type(body) == NODE_FBODY) {
	    *klassp = body->nd_orig;
	    *idp = body->nd_mid;
	    body = body->nd_head;
	}
	else {
	    *klassp = origin;
	}
    }

    return body;
}

static void
remove_method(klass, mid)
    VALUE klass;
    ID mid;
{
    NODE *body;

    if (klass == rb_cObject) {
	rb_secure(4);
    }
    if (ruby_safe_level >= 4 && !OBJ_TAINTED(klass)) {
	rb_raise(rb_eSecurityError, "Insecure: can't remove method");
    }
    if (OBJ_FROZEN(klass)) rb_error_frozen("class/module");
    if (mid == __id__ || mid == __send__ || mid == init) {
	rb_warn("removing `%s' may cause serious problem", rb_id2name(mid));
    }
    if (!st_delete(RCLASS(klass)->m_tbl, &mid, (st_data_t *)&body) ||
	!body->nd_body) {
	rb_name_error(mid, "method `%s' not defined in %s",
		      rb_id2name(mid), rb_class2name(klass));
    }
    rb_clear_cache_for_undef(klass, mid);
    if (FL_TEST(klass, FL_SINGLETON)) {
	rb_funcall(rb_iv_get(klass, "__attached__"), singleton_removed, 1, ID2SYM(mid));
    }
    else {
	rb_funcall(klass, removed, 1, ID2SYM(mid));
    }
}

void
rb_remove_method(klass, name)
    VALUE klass;
    const char *name;
{
    remove_method(klass, rb_intern(name));
}

static VALUE
rb_mod_remove_method(argc, argv, mod)
    int argc;
    VALUE *argv;
    VALUE mod;
{
    int i;

    for (i=0; i<argc; i++) {
	remove_method(mod, rb_to_id(argv[i]));
    }
    return mod;
}

void
rb_disable_super(klass, name)
    VALUE klass;
    const char *name;
{
    /* obsolete - no use */
}

void
rb_enable_super(klass, name)
    VALUE klass;
    const char *name;
{
    rb_warning("rb_enable_super() is obsolete");
}

static void
rb_export_method(klass, name, noex)
    VALUE klass;
    ID name;
    ID noex;
{
    NODE *body;
    VALUE origin;

    if (klass == rb_cObject) {
	rb_secure(4);
    }
    body = search_method(klass, name, &origin);
    if (!body && TYPE(klass) == T_MODULE) {
	body = search_method(rb_cObject, name, &origin);
    }
    if (!body || !body->nd_body) {
	print_undef(klass, name);
    }
    if (body->nd_noex != noex) {
	if (klass == origin) {
	    body->nd_noex = noex;
	}
	else {
	    rb_add_method(klass, name, NEW_ZSUPER(), noex);
	}
    }
}

int
rb_method_boundp(klass, id, ex)
    VALUE klass;
    ID id;
    int ex;
{
    struct cache_entry *ent;
    int noex;

    /* is it in the method cache? */
    ent = cache + EXPR1(klass, id);
    if (ent->mid == id && ent->klass == klass) {
	if (ex && (ent->noex & NOEX_PRIVATE))
	    return Qfalse;
	if (!ent->method) return Qfalse;
	return Qtrue;
    }
    if (rb_get_method_body(&klass, &id, &noex)) {
	if (ex && (noex & NOEX_PRIVATE))
	    return Qfalse;
	return Qtrue;
    }
    return Qfalse;
}

void
rb_attr(klass, id, read, write, ex)
    VALUE klass;
    ID id;
    int read, write, ex;
{
    const char *name;
    char *buf;
    ID attriv;
    int noex;

    if (!ex) noex = NOEX_PUBLIC;
    else {
	if (SCOPE_TEST(SCOPE_PRIVATE)) {
	    noex = NOEX_PRIVATE;
	    rb_warning((scope_vmode == SCOPE_MODFUNC) ?
		       "attribute accessor as module_function" :
		       "private attribute?");
	}
	else if (SCOPE_TEST(SCOPE_PROTECTED)) {
	    noex = NOEX_PROTECTED;
	}
	else {
	    noex = NOEX_PUBLIC;
	}
    }

    name = rb_id2name(id);
    if (!name) {
	rb_raise(rb_eArgError, "argument needs to be symbol or string");
    }
    buf = ALLOCA_N(char,strlen(name)+2);
    sprintf(buf, "@%s", name);
    attriv = rb_intern(buf);
    if (read) {
	rb_add_method(klass, id, NEW_IVAR(attriv), noex);
    }
    if (write) {
	sprintf(buf, "%s=", name);
	id = rb_intern(buf);
	rb_add_method(klass, id, NEW_ATTRSET(attriv), noex);
    }
}

extern int ruby_in_compile;

VALUE ruby_errinfo = Qnil;
extern NODE *ruby_eval_tree_begin;
extern NODE *ruby_eval_tree;
extern int ruby_nerrs;

static VALUE rb_eLocalJumpError;
static VALUE rb_eSysStackError;

extern VALUE ruby_top_self;

struct FRAME *ruby_frame;
struct SCOPE *ruby_scope;
static struct FRAME *top_frame;
static struct SCOPE *top_scope;

#define PUSH_FRAME() do {		\
    struct FRAME _frame;		\
    _frame.prev = ruby_frame;		\
    _frame.tmp  = 0;			\
    _frame.node = ruby_current_node;	\
    _frame.iter = ruby_iter->iter;	\
    _frame.cbase = ruby_frame->cbase;	\
    _frame.argc = 0;			\
    _frame.argv = 0;			\
    _frame.flags = FRAME_ALLOCA;	\
    ruby_frame = &_frame

#define POP_FRAME()  			\
    ruby_current_node = _frame.node;	\
    ruby_frame = _frame.prev;		\
} while (0)

struct BLOCKTAG {
    struct RBasic super;
    long dst;
    long flags;
};

struct BLOCK {
    NODE *var;
    NODE *body;
    VALUE self;
    struct FRAME frame;
    struct SCOPE *scope;
    struct BLOCKTAG *tag;
    VALUE klass;
    int iter;
    int vmode;
    int flags;
    struct RVarmap *dyna_vars;
    VALUE orig_thread;
    VALUE wrapper;
    VALUE block_obj;
    struct BLOCK *outer;
    struct BLOCK *prev;
};

#define BLOCK_D_SCOPE 1
#define BLOCK_DYNAMIC 2
#define BLOCK_ORPHAN  4
#define BLOCK_LAMBDA  8

static struct BLOCK *ruby_block;

static struct BLOCKTAG*
new_blktag()
{
    NEWOBJ(blktag, struct BLOCKTAG);
    OBJSETUP(blktag, 0, T_BLKTAG);
    blktag->dst = 0;
    blktag->flags = 0;
    return blktag;
}

#define PUSH_BLOCK(v,b) do {		\
    struct BLOCK _block;		\
    _block.tag = new_blktag();		\
    _block.var = v;			\
    _block.body = b;			\
    _block.self = self;			\
    _block.frame = *ruby_frame;		\
    _block.klass = ruby_class;		\
    _block.frame.node = ruby_current_node;\
    _block.scope = ruby_scope;		\
    _block.prev = ruby_block;		\
    _block.outer = ruby_block;		\
    _block.iter = ruby_iter->iter;	\
    _block.vmode = scope_vmode;		\
    _block.flags = BLOCK_D_SCOPE;	\
    _block.dyna_vars = ruby_dyna_vars;	\
    _block.wrapper = ruby_wrapper;	\
    _block.block_obj = 0;		\
    ruby_block = &_block

#define POP_BLOCK() 			\
   if (_block.tag->flags & (BLOCK_DYNAMIC)) \
       _block.tag->flags |= BLOCK_ORPHAN; \
   else	if (!(_block.scope->flags & SCOPE_DONT_RECYCLE)) \
       rb_gc_force_recycle((VALUE)_block.tag); \
   ruby_block = _block.prev; 		\
} while (0)

struct RVarmap *ruby_dyna_vars;
#define PUSH_VARS() do { \
    struct RVarmap * volatile _old; \
    _old = ruby_dyna_vars; \
    ruby_dyna_vars = 0

#define POP_VARS() \
   if (_old && (ruby_scope->flags & SCOPE_DONT_RECYCLE)) {\
       if (RBASIC(_old)->flags) /* unless it's already recycled */ \
           FL_SET(_old, DVAR_DONT_RECYCLE); \
    }\
    ruby_dyna_vars = _old; \
} while (0)

#define DVAR_DONT_RECYCLE FL_USER2

static struct RVarmap*
new_dvar(id, value, prev)
    ID id;
    VALUE value;
    struct RVarmap *prev;
{
    NEWOBJ(vars, struct RVarmap);
    OBJSETUP(vars, 0, T_VARMAP);
    vars->id = id;
    vars->val = value;
    vars->next = prev;

    return vars;
}

VALUE
rb_dvar_defined(id)
    ID id;
{
    struct RVarmap *vars = ruby_dyna_vars;

    while (vars) {
	if (vars->id == id) return Qtrue;
	vars = vars->next;
    }
    return Qfalse;
}

VALUE
rb_dvar_curr(id)
    ID id;
{
    struct RVarmap *vars = ruby_dyna_vars;

    while (vars) {
	if (vars->id == 0) break;
	if (vars->id == id) return Qtrue;
	vars = vars->next;
    }
    return Qfalse;
}

VALUE
rb_dvar_ref(id)
    ID id;
{
    struct RVarmap *vars = ruby_dyna_vars;

    while (vars) {
	if (vars->id == id) {
	    return vars->val;
	}
	vars = vars->next;
    }
    return Qnil;
}

void
rb_dvar_push(id, value)
    ID id;
    VALUE value;
{
    ruby_dyna_vars = new_dvar(id, value, ruby_dyna_vars);
}

static void
dvar_asgn_internal(id, value, curr)
    ID id;
    VALUE value;
    int curr;
{
    int n = 0;
    struct RVarmap *vars = ruby_dyna_vars;

    while (vars) {
	if (curr && vars->id == 0) {
	    /* first null is a dvar header */
	    n++;
	    if (n == 2) break;
	}
	if (vars->id == id) {
	    vars->val = value;
	    return;
	}
	vars = vars->next;
    }
    if (!ruby_dyna_vars) {
	ruby_dyna_vars = new_dvar(id, value, 0);
    }
    else {
	vars = new_dvar(id, value, ruby_dyna_vars->next);
	ruby_dyna_vars->next = vars;
    }
}

static inline void
dvar_asgn(id, value)
    ID id;
    VALUE value;
{
    dvar_asgn_internal(id, value, 0);
}

static inline void
dvar_asgn_curr(id, value)
    ID id;
    VALUE value;
{
    dvar_asgn_internal(id, value, 1);
}

VALUE *
rb_svar(cnt)
    int cnt;
{
    struct RVarmap *vars = ruby_dyna_vars;
    ID id;

    if (!ruby_scope->local_tbl) return NULL;
    if (cnt >= ruby_scope->local_tbl[0]) return NULL;
    id = ruby_scope->local_tbl[cnt+1];
    while (vars) {
	if (vars->id == id) return &vars->val;
	vars = vars->next;
    }
    if (ruby_scope->local_vars == 0) return NULL;
    return &ruby_scope->local_vars[cnt];
}

struct iter {
    int iter;
    struct iter *prev;
};
static struct iter *ruby_iter;

#define ITER_NOT 0
#define ITER_PRE 1
#define ITER_CUR 2

#define PUSH_ITER(i) do {		\
    struct iter _iter;			\
    _iter.prev = ruby_iter;		\
    _iter.iter = (i);			\
    ruby_iter = &_iter

#define POP_ITER()			\
    ruby_iter = _iter.prev;		\
} while (0)

struct tag {
    jmp_buf buf;
    struct FRAME *frame;
    struct iter *iter;
    ID tag;
    VALUE retval;
    struct SCOPE *scope;
    int dst;
    struct tag *prev;
};
static struct tag *prot_tag;

#define PUSH_TAG(ptag) do {		\
    struct tag _tag;			\
    _tag.retval = Qnil;			\
    _tag.frame = ruby_frame;		\
    _tag.iter = ruby_iter;		\
    _tag.prev = prot_tag;		\
    _tag.scope = ruby_scope;		\
    _tag.tag = ptag;			\
    _tag.dst = 0;			\
    prot_tag = &_tag

#define PROT_NONE   0
#define PROT_FUNC   -1
#define PROT_THREAD -2

#define EXEC_TAG()    (FLUSH_REGISTER_WINDOWS, setjmp(prot_tag->buf))

#define JUMP_TAG(st) do {		\
    ruby_frame = prot_tag->frame;	\
    ruby_iter = prot_tag->iter;		\
    longjmp(prot_tag->buf,(st));	\
} while (0)

#define POP_TAG()			\
    if (_tag.prev)			\
        _tag.prev->retval = _tag.retval;\
    prot_tag = _tag.prev;		\
} while (0)

#define POP_TMPTAG()			\
    prot_tag = _tag.prev;		\
} while (0)

#define TAG_RETURN	0x1
#define TAG_BREAK	0x2
#define TAG_NEXT	0x3
#define TAG_RETRY	0x4
#define TAG_REDO	0x5
#define TAG_RAISE	0x6
#define TAG_THROW	0x7
#define TAG_FATAL	0x8
#define TAG_MASK	0xf

VALUE ruby_class;
static VALUE ruby_wrapper;	/* security wrapper */

#define PUSH_CLASS() do {		\
    VALUE _class = ruby_class

#define POP_CLASS() ruby_class = _class; \
} while (0)

static NODE *ruby_cref = 0;
static NODE *top_cref;
#define PUSH_CREF(c) ruby_cref = NEW_NODE(NODE_CREF,(c),0,ruby_cref)
#define POP_CREF() ruby_cref = ruby_cref->nd_next

#define PUSH_SCOPE() do {		\
    volatile int _vmode = scope_vmode;	\
    struct SCOPE * volatile _old;	\
    NEWOBJ(_scope, struct SCOPE);	\
    OBJSETUP(_scope, 0, T_SCOPE);	\
    _scope->local_tbl = 0;		\
    _scope->local_vars = 0;		\
    _scope->flags = 0;			\
    _old = ruby_scope;			\
    ruby_scope = _scope;		\
    scope_vmode = SCOPE_PUBLIC

typedef struct thread * rb_thread_t;
static rb_thread_t curr_thread = 0;
static rb_thread_t main_thread;
static void scope_dup _((struct SCOPE *));

#define POP_SCOPE() 			\
    if (ruby_scope->flags & SCOPE_DONT_RECYCLE) {\
       if (_old) scope_dup(_old);	\
    }					\
    if (!(ruby_scope->flags & SCOPE_MALLOC)) {\
	ruby_scope->local_vars = 0;	\
	ruby_scope->local_tbl  = 0;	\
	if (!(ruby_scope->flags & SCOPE_DONT_RECYCLE) && \
            ruby_scope != top_scope) {	\
	    rb_gc_force_recycle((VALUE)ruby_scope);\
        }				\
    }					\
    ruby_scope->flags |= SCOPE_NOSTACK;	\
    ruby_scope = _old;			\
    scope_vmode = _vmode;		\
} while (0)

static VALUE rb_eval _((VALUE,NODE*));
static VALUE eval _((VALUE,VALUE,VALUE,char*,int));
static NODE *compile _((VALUE, char*, int));

static VALUE rb_yield_0 _((VALUE, VALUE, VALUE, int, int));

#define YIELD_PROC_CALL  1
#define YIELD_PUBLIC_DEF 2
#define YIELD_FUNC_AVALUE 1
#define YIELD_FUNC_SVALUE 2

static VALUE rb_call _((VALUE,VALUE,ID,int,const VALUE*,int));
static VALUE module_setup _((VALUE,NODE*));

static VALUE massign _((VALUE,NODE*,VALUE,int));
static void assign _((VALUE,NODE*,VALUE,int));

static VALUE trace_func = 0;
static int tracing = 0;
static void call_trace_func _((char*,NODE*,VALUE,ID,VALUE));

#if 0
#define SET_CURRENT_SOURCE() (ruby_sourcefile = ruby_current_node->nd_file, \
			      ruby_sourceline = nd_line(ruby_current_node))
#else
#define SET_CURRENT_SOURCE() ((void)0)
#endif

void
ruby_set_current_source()
{
    if (ruby_current_node) {
        ruby_sourcefile = ruby_current_node->nd_file;
	ruby_sourceline = nd_line(ruby_current_node);
    }
}

static void
#ifdef HAVE_STDARG_PROTOTYPES
warn_printf(const char *fmt, ...)
#else
warn_printf(fmt, va_alist)
    const char *fmt;
    va_dcl
#endif
{
    char buf[BUFSIZ];
    va_list args;

    va_init_list(args, fmt);
    vsnprintf(buf, BUFSIZ, fmt, args);
    va_end(args);
    rb_write_error(buf);
}

#define warn_print(x) rb_write_error(x)
#define warn_print2(x,l) rb_write_error2(x,l)

static void
error_pos()
{
    ruby_set_current_source();
    if (ruby_sourcefile) {
	if (ruby_frame->last_func) {
	    warn_printf("%s:%d:in `%s'", ruby_sourcefile, ruby_sourceline,
			rb_id2name(ruby_frame->last_func));
	}
	else if (ruby_sourceline == 0) {
	    warn_printf("%s", ruby_sourcefile);
	}
	else {
	    warn_printf("%s:%d", ruby_sourcefile, ruby_sourceline);
	}
    }
}

static VALUE
get_backtrace(info)
    VALUE info;
{
    if (NIL_P(info)) return Qnil;
    return rb_funcall(info, rb_intern("backtrace"), 0);
}

static void
set_backtrace(info, bt)
    VALUE info, bt;
{
    rb_funcall(info, rb_intern("set_backtrace"), 1, bt);
}

static void
error_print()
{
    VALUE errat = Qnil;		/* OK */
    volatile VALUE eclass, e;
    char *einfo;
    long elen;

    if (NIL_P(ruby_errinfo)) return;

    PUSH_TAG(PROT_NONE);
    if (EXEC_TAG() == 0) {
	errat = get_backtrace(ruby_errinfo);
    }
    else {
	errat = Qnil;
    }
    if (EXEC_TAG()) goto error;
    if (NIL_P(errat)){
	ruby_set_current_source();
	if (ruby_sourcefile)
	    warn_printf("%s:%d", ruby_sourcefile, ruby_sourceline);
	else
	    warn_printf("%d", ruby_sourceline);
    }
    else if (RARRAY(errat)->len == 0) {
	error_pos();
    }
    else {
	VALUE mesg = RARRAY(errat)->ptr[0];

	if (NIL_P(mesg)) error_pos();
	else {
	    warn_print2(RSTRING(mesg)->ptr, RSTRING(mesg)->len);
	}
    }

    eclass = CLASS_OF(ruby_errinfo);
    if (EXEC_TAG() == 0) {
	e = rb_obj_as_string(ruby_errinfo);
	einfo = RSTRING(e)->ptr;
	elen = RSTRING(e)->len;
    }
    else {
	einfo = "";
	elen = 0;
    }
    if (EXEC_TAG()) goto error;
    if (eclass == rb_eRuntimeError && elen == 0) {
	warn_print(": unhandled exception\n");
    }
    else {
	VALUE epath;

	epath = rb_class_path(eclass);
	if (elen == 0) {
	    warn_print(": ");
	    warn_print2(RSTRING(epath)->ptr, RSTRING(epath)->len);
	}
	else {
	    char *tail  = 0;
	    long len = elen;

	    if (RSTRING(epath)->ptr[0] == '#') epath = 0;
	    if (tail = memchr(einfo, '\n', elen)) {
		len = tail - einfo;
		tail++;		/* skip newline */
	    }
	    warn_print(": ");
	    warn_print2(einfo, len);
	    if (epath) {
		warn_print(" (");
		warn_print2(RSTRING(epath)->ptr, RSTRING(epath)->len);
		warn_print(")\n");
	    }
	    if (tail) {
		warn_print2(tail, elen-len-1);
	    }
	}
    }

    if (!NIL_P(errat)) {
	long i;
	struct RArray *ep = RARRAY(errat);

#define TRACE_MAX (TRACE_HEAD+TRACE_TAIL+5)
#define TRACE_HEAD 8
#define TRACE_TAIL 5

	ep = RARRAY(errat);
	for (i=1; i<ep->len; i++) {
	    if (TYPE(ep->ptr[i]) == T_STRING) {
		warn_printf("\tfrom %s\n", RSTRING(ep->ptr[i])->ptr);
	    }
	    if (i == TRACE_HEAD && ep->len > TRACE_MAX) {
		warn_printf("\t ... %ld levels...\n",
			ep->len - TRACE_HEAD - TRACE_TAIL);
		i = ep->len - TRACE_TAIL;
	    }
	}
    }
  error:
    POP_TAG();
}

#if defined(__APPLE__)
#define environ (*_NSGetEnviron())
#elif !defined(_WIN32) && !defined(__MACOS__) || defined(_WIN32_WCE)
extern char **environ;
#endif
char **rb_origenviron;

void rb_call_inits _((void));
void Init_stack _((void*));
void Init_heap _((void));
void Init_ext _((void));

void
ruby_init()
{
    static int initialized = 0;
    static struct FRAME frame;
    static struct iter iter;
    int state;

    if (initialized)
	return;
    initialized = 1;

    ruby_frame = top_frame = &frame;
    ruby_iter = &iter;

#ifdef __MACOS__
    rb_origenviron = 0;
#elif defined(__APPLE__)
    rb_origenviron = *_NSGetEnviron();
#else
    rb_origenviron = environ;
#endif

    Init_stack((void*)&state);
    Init_heap();
    PUSH_SCOPE();
    ruby_scope->local_vars = 0;
    ruby_scope->local_tbl  = 0;
    top_scope = ruby_scope;
    /* default visibility is private at toplevel */
    SCOPE_SET(SCOPE_PRIVATE);

    PUSH_TAG(PROT_NONE);
    if ((state = EXEC_TAG()) == 0) {
	rb_call_inits();
	ruby_class = rb_cObject;
	ruby_frame->self = ruby_top_self;
	top_cref = rb_node_newnode(NODE_CREF,rb_cObject,0,0);
	ruby_cref = top_cref;
	ruby_frame->cbase = (VALUE)ruby_cref;
	rb_define_global_const("TOPLEVEL_BINDING", rb_f_binding(ruby_top_self));
#ifdef __MACOS__
	_macruby_init();
#endif
	ruby_prog_init();
	ALLOW_INTS;
    }
    POP_TAG();
    if (state) {
	error_print();
	exit(1);
    }
    POP_SCOPE();
    ruby_scope = top_scope;
    ruby_running = 1;
}

static VALUE
eval_node(self, node)
    VALUE self;
    NODE *node;
{
    NODE *beg_tree = ruby_eval_tree_begin;

    ruby_eval_tree_begin = 0;
    if (beg_tree) {
	rb_eval(self, beg_tree);
    }

    if (!node) return Qnil;
    return rb_eval(self, node);
}

int ruby_in_eval;

static void rb_thread_cleanup _((void));
static void rb_thread_wait_other_threads _((void));

static int thread_set_raised();
static int thread_reset_raised();

static VALUE exception_error;
static VALUE sysstack_error;

static int
error_handle(ex)
    int ex;
{
    if (thread_set_raised()) return 1;

    switch (ex & TAG_MASK) {
      case 0:
	ex = 0;
	break;

      case TAG_RETURN:
	error_pos();
	warn_print(": unexpected return\n");
	ex = 1;
	break;
      case TAG_NEXT:
	error_pos();
	warn_print(": unexpected next\n");
	ex = 1;
	break;
      case TAG_BREAK:
	error_pos();
	warn_print(": unexpected break\n");
	ex = 1;
	break;
      case TAG_REDO:
	error_pos();
	warn_print(": unexpected redo\n");
	ex = 1;
	break;
      case TAG_RETRY:
	error_pos();
	warn_print(": retry outside of rescue clause\n");
	ex = 1;
	break;
      case TAG_THROW:
	if (prot_tag && prot_tag->frame && prot_tag->frame->node) {
	    NODE *tag = prot_tag->frame->node;
	    warn_printf("%s:%d: uncaught throw\n",
		    tag->nd_file, nd_line(tag));
	}
	else {
	    error_pos();
	    warn_printf(": unexpected throw\n");
	}
	ex = 1;
	break;
      case TAG_RAISE:
      case TAG_FATAL:
	if (rb_obj_is_kind_of(ruby_errinfo, rb_eSystemExit)) {
	    VALUE st = rb_iv_get(ruby_errinfo, "status");
	    ex = NUM2INT(st);
	}
	else {
	    error_print();
	    ex = 1;
	}
	break;
      default:
	rb_bug("Unknown longjmp status %d", ex);
	break;
    }
    thread_reset_raised();
    return ex;
}

void
ruby_options(argc, argv)
    int argc;
    char **argv;
{
    int state;

    Init_stack((void*)&state);
    PUSH_TAG(PROT_NONE);
    if ((state = EXEC_TAG()) == 0) {
	ruby_process_options(argc, argv);
    }
    if (state) {
	trace_func = 0;
	tracing = 0;
	exit(error_handle(state));
    }
    POP_TAG();
}

void rb_exec_end_proc _((void));

static void
ruby_finalize_0(exp)
    int *exp;
{
    ruby_errinfo = 0;
    PUSH_TAG(PROT_NONE);
    if (EXEC_TAG() == 0) {
	rb_trap_exit();
    }
    POP_TAG();
    rb_exec_end_proc();
    rb_gc_call_finalizer_at_exit();
    if (exp && ruby_errinfo && rb_obj_is_kind_of(ruby_errinfo, rb_eSystemExit)) {
	VALUE st = rb_iv_get(ruby_errinfo, "status");
	*exp = NUM2INT(st);
    }
    trace_func = 0;
    tracing = 0;
}

void
ruby_finalize()
{
    ruby_finalize_0(0);
}

int
ruby_cleanup(ex)
    int ex;
{
    int state;

    ruby_safe_level = 0;
    PUSH_TAG(PROT_NONE);
    PUSH_ITER(ITER_NOT);
    if ((state = EXEC_TAG()) == 0) {
	rb_thread_cleanup();
	rb_thread_wait_other_threads();
    }
    else if (ex == 0) {
	ex = state;
    }   
    POP_ITER();

    ex = error_handle(ex);
    POP_TAG();
    ruby_finalize_0(&ex);
    return ex;
}

int
ruby_exec()
{
    int state;
    volatile NODE *tmp;

    Init_stack((void*)&tmp);
    PUSH_TAG(PROT_NONE);
    PUSH_ITER(ITER_NOT);
    /* default visibility is private at toplevel */
    SCOPE_SET(SCOPE_PRIVATE);
    if ((state = EXEC_TAG()) == 0) {
	eval_node(ruby_top_self, ruby_eval_tree);
    }
    POP_ITER();
    POP_TAG();
    return state;
}

void
ruby_stop(ex)
    int ex;
{
    exit(ruby_cleanup(ex));
}

void
ruby_run()
{
    int state;
    static int ex;

    if (ruby_nerrs > 0) exit(ruby_nerrs);
    state = ruby_exec();
    if (state && !ex) ex = state;
    ruby_stop(ex);
}

static void
compile_error(at)
    const char *at;
{
    VALUE str;

    ruby_nerrs = 0;
    str = rb_str_buf_new2("compile error");
    if (at) {
	rb_str_buf_cat2(str, " in ");
	rb_str_buf_cat2(str, at);
    }
    rb_str_buf_cat(str, "\n", 1);
    if (!NIL_P(ruby_errinfo)) {
	rb_str_append(str, rb_obj_as_string(ruby_errinfo));
    }
    rb_exc_raise(rb_exc_new3(rb_eSyntaxError, str));
}

VALUE
rb_eval_string(str)
    const char *str;
{
    VALUE v;
    NODE *oldsrc = ruby_current_node;

    ruby_current_node = 0;
    ruby_sourcefile = rb_source_filename("(eval)");
    v = eval(ruby_top_self, rb_str_new2(str), Qnil, 0, 0);
    ruby_current_node = oldsrc;

    return v;
}

VALUE
rb_eval_string_protect(str, state)
    const char *str;
    int *state;
{
    VALUE result;		/* OK */
    int status;

    PUSH_TAG(PROT_NONE);
    if ((status = EXEC_TAG()) == 0) {
	result = rb_eval_string(str);
    }
    POP_TAG();
    if (state) {
	*state = status;
    }
    if (status != 0) {
	return Qnil;
    }

    return result;
}

VALUE
rb_eval_string_wrap(str, state)
    const char *str;
    int *state;
{
    int status;
    VALUE self = ruby_top_self;
    VALUE wrapper = ruby_wrapper;
    VALUE val;

    PUSH_CLASS();
    ruby_class = ruby_wrapper = rb_module_new();
    ruby_top_self = rb_obj_clone(ruby_top_self);
    rb_extend_object(ruby_top_self, ruby_wrapper);
    PUSH_FRAME();
    ruby_frame->last_func = 0;
    ruby_frame->orig_func = 0;
    ruby_frame->last_class = 0;
    ruby_frame->self = self;
    ruby_frame->cbase = (VALUE)rb_node_newnode(NODE_CREF,ruby_wrapper,0,0);
    PUSH_SCOPE();

    val = rb_eval_string_protect(str, &status);
    ruby_top_self = self;

    POP_SCOPE();
    POP_FRAME();
    POP_CLASS();
    ruby_wrapper = wrapper;
    if (state) {
	*state = status;
    }
    else if (status) {
	JUMP_TAG(status);
    }
    return val;
}

static void
localjump_error(mesg, status, reason)
    const char *mesg;
    VALUE status;
    int reason;
{
    VALUE exc = rb_exc_new2(rb_eLocalJumpError, mesg);
    VALUE id;

    rb_iv_set(exc, "@exit_value", status);
    switch (reason) {
      case TAG_BREAK:
	id = rb_intern("break"); break;
      case TAG_REDO:
	id = rb_intern("redo"); break;
      case TAG_RETRY:
	id = rb_intern("retry"); break;
      case TAG_NEXT:
	id = rb_intern("next"); break;
      case TAG_RETURN:
	id = rb_intern("return"); break;
      default:
	id = rb_intern("noreason"); break;
    }
    rb_iv_set(exc, "@reason", ID2SYM(id));
    rb_exc_raise(exc);
}

static VALUE
localjump_xvalue(exc)
    VALUE exc;
{
    return rb_iv_get(exc, "@exit_value");
}

static VALUE
localjump_reason(exc)
    VALUE exc;
{
    return rb_iv_get(exc, "@reason");
}

static void
jump_tag_but_local_jump(state)
    int state;
{
    VALUE val;

    if (prot_tag) val = prot_tag->retval;
    else          val = Qnil;
    switch (state) {
      case 0:
	break;
      case TAG_RETURN:
	localjump_error("unexpected return", val, state);
	break;
      case TAG_NEXT:
	localjump_error("unexpected next", val, state);
	break;
      case TAG_BREAK:
	localjump_error("unexpected break", val, state);
	break;
      case TAG_REDO:
	localjump_error("unexpected redo", Qnil, state);
	break;
      case TAG_RETRY:
	localjump_error("retry outside of rescue clause", Qnil, state);
	break;
      default:
	JUMP_TAG(state);
	break;
    }
}

VALUE
rb_eval_cmd(cmd, arg, tcheck)
    VALUE cmd, arg;
    int tcheck;
{
    int state;
    VALUE val;			/* OK */
    struct SCOPE *saved_scope;
    volatile int safe = ruby_safe_level;

    if (TYPE(cmd) != T_STRING) {
	PUSH_ITER(ITER_NOT);
	val = rb_funcall2(cmd, rb_intern("call"), RARRAY(arg)->len, RARRAY(arg)->ptr);
	POP_ITER();
	return val;
    }

    saved_scope = ruby_scope;
    ruby_scope = top_scope;
    PUSH_FRAME();
    ruby_frame->last_func = 0;
    ruby_frame->orig_func = 0;
    ruby_frame->last_class = 0;
    ruby_frame->self = ruby_top_self;
    ruby_frame->cbase = (VALUE)rb_node_newnode(NODE_CREF,0,0,0);
    RNODE(ruby_frame->cbase)->nd_clss = ruby_wrapper ? ruby_wrapper : rb_cObject;

    if (tcheck && OBJ_TAINTED(cmd)) {
	ruby_safe_level = 4;
    }

    PUSH_TAG(PROT_NONE);
    if ((state = EXEC_TAG()) == 0) {
	val = eval(ruby_top_self, cmd, Qnil, 0, 0);
    }
    if (ruby_scope->flags & SCOPE_DONT_RECYCLE)
       scope_dup(saved_scope);
    ruby_scope = saved_scope;
    ruby_safe_level = safe;
    POP_TAG();
    POP_FRAME();

    jump_tag_but_local_jump(state);
    return val;
}

static VALUE
superclass(self, node)
    VALUE self;
    NODE *node;
{
    VALUE val;			/* OK */
    int state;

    PUSH_TAG(PROT_NONE);
    if ((state = EXEC_TAG()) == 0) {
	val = rb_eval(self, node);
    }
    POP_TAG();
    if (state) {
	switch (nd_type(node)) {
	  case NODE_COLON2:
	    rb_raise(rb_eTypeError, "undefined superclass `%s'",
		     rb_id2name(node->nd_mid));
	  case NODE_CONST:
	    rb_raise(rb_eTypeError, "undefined superclass `%s'",
		     rb_id2name(node->nd_vid));
	  default:
	    break;
	}
	JUMP_TAG(state);
    }
    if (TYPE(val) != T_CLASS) {
	rb_raise(rb_eTypeError, "superclass must be a Class (%s given)",
		 rb_obj_classname(val));
    }
    if (FL_TEST(val, FL_SINGLETON)) {
	rb_raise(rb_eTypeError, "can't make subclass of virtual class");
    }

    return val;
}

#define ruby_cbase (RNODE(ruby_frame->cbase)->nd_clss)

static VALUE
ev_const_defined(cref, id, self)
    NODE *cref;
    ID id;
    VALUE self;
{
    NODE *cbase = cref;
    VALUE result;

    while (cbase && cbase->nd_next) {
	struct RClass *klass = RCLASS(cbase->nd_clss);

	if (NIL_P(klass)) return rb_const_defined(CLASS_OF(self), id);
	if (klass->iv_tbl && st_lookup(klass->iv_tbl, id, &result)) {
	    if (result == Qundef && NIL_P(rb_autoload_p((VALUE)klass, id))) {
		return Qfalse;
	    }
	    return Qtrue;
	}
	cbase = cbase->nd_next;
    }
    return rb_const_defined(cref->nd_clss, id);
}

static VALUE
ev_const_get(cref, id, self)
    NODE *cref;
    ID id;
    VALUE self;
{
    NODE *cbase = cref;
    VALUE result;

    while (cbase && cbase->nd_next) {
	VALUE klass = cbase->nd_clss;

	if (NIL_P(klass)) return rb_const_get(CLASS_OF(self), id);
	while (RCLASS(klass)->iv_tbl && st_lookup(RCLASS(klass)->iv_tbl, id, &result)) {
	    if (result == Qundef) {
		rb_autoload_load(klass, id);
		continue;
	    }
	    return result;
	}
	cbase = cbase->nd_next;
    }
    return rb_const_get(cref->nd_clss, id);
}

static VALUE
cvar_cbase()
{
    NODE *cref = RNODE(ruby_frame->cbase);

    while (cref && cref->nd_next && FL_TEST(cref->nd_clss, FL_SINGLETON)) {
	cref = cref->nd_next;
	if (!cref->nd_next) {
	    rb_warn("class variable access from toplevel singleton method");
	}
    }
    return cref->nd_clss;
}

static VALUE
rb_mod_nesting()
{
    NODE *cbase = RNODE(ruby_frame->cbase);
    VALUE ary = rb_ary_new();

    while (cbase && cbase->nd_next) {
	if (!NIL_P(cbase->nd_clss)) rb_ary_push(ary, cbase->nd_clss);
	cbase = cbase->nd_next;
    }
    if (ruby_wrapper && RARRAY(ary)->len == 0) {
	rb_ary_push(ary, ruby_wrapper);
    }
    return ary;
}

static VALUE
rb_mod_s_constants()
{
    NODE *cbase = RNODE(ruby_frame->cbase);
    void *data = 0;

    while (cbase) {
	if (!NIL_P(cbase->nd_clss)) {
	    data = rb_mod_const_at(cbase->nd_clss, data);
	}
	cbase = cbase->nd_next;
    }

    if (!NIL_P(ruby_cbase)) {
	data = rb_mod_const_of(ruby_cbase, data);
    }
    return rb_const_list(data);
}

void
rb_frozen_class_p(klass)
    VALUE klass;
{
    char *desc = "something(?!)";

    if (OBJ_FROZEN(klass)) {
	if (FL_TEST(klass, FL_SINGLETON))
	    desc = "object";
	else {
	    switch (TYPE(klass)) {
	      case T_MODULE:
	      case T_ICLASS:
		desc = "module"; break;
	      case T_CLASS:
		desc = "class"; break;
	    }
	}
	rb_error_frozen(desc);
    }
}

void
rb_undef(klass, id)
    VALUE klass;
    ID id;
{
    VALUE origin;
    NODE *body;

    if (ruby_class == rb_cObject && klass == ruby_class) {
	rb_secure(4);
    }
    if (ruby_safe_level >= 4 && !OBJ_TAINTED(klass)) {
	rb_raise(rb_eSecurityError, "Insecure: can't undef `%s'", rb_id2name(id));
    }
    rb_frozen_class_p(klass);
    if (id == __id__ || id == __send__ || id == init) {
	rb_warn("undefining `%s' may cause serious problem", rb_id2name(id));
    }
    body = search_method(klass, id, &origin);
    if (!body || !body->nd_body) {
	char *s0 = " class";
	VALUE c = klass;

	if (FL_TEST(c, FL_SINGLETON)) {
	    VALUE obj = rb_iv_get(klass, "__attached__");

	    switch (TYPE(obj)) {
	      case T_MODULE:
	      case T_CLASS:
		c = obj;
		s0 = "";
	    }
	}
	else if (TYPE(c) == T_MODULE) {
	    s0 = " module";
	}
	rb_name_error(id, "undefined method `%s' for%s `%s'",
		      rb_id2name(id),s0,rb_class2name(c));
    }
    rb_add_method(klass, id, 0, NOEX_PUBLIC);
    if (FL_TEST(klass, FL_SINGLETON)) {
	rb_funcall(rb_iv_get(klass, "__attached__"),
		   singleton_undefined, 1, ID2SYM(id));
    }
    else {
	rb_funcall(klass, undefined, 1, ID2SYM(id));
    }
}

static VALUE
rb_mod_undef_method(argc, argv, mod)
    int argc;
    VALUE *argv;
    VALUE mod;
{
    int i;

    for (i=0; i<argc; i++) {
	rb_undef(mod, rb_to_id(argv[i]));
    }
    return mod;
}

void
rb_alias(klass, name, def)
    VALUE klass;
    ID name, def;
{
    VALUE origin;
    NODE *orig, *body;
    VALUE singleton = 0;

    rb_frozen_class_p(klass);
    if (name == def) return;
    if (klass == rb_cObject) {
	rb_secure(4);
    }
    orig = search_method(klass, def, &origin);
    if (!orig || !orig->nd_body) {
	if (TYPE(klass) == T_MODULE) {
	    orig = search_method(rb_cObject, def, &origin);
	}
    }
    if (!orig || !orig->nd_body) {
	print_undef(klass, def);
    }
    if (FL_TEST(klass, FL_SINGLETON)) {
	singleton = rb_iv_get(klass, "__attached__");
    }
    body = orig->nd_body;
    orig->nd_cnt++;
    if (nd_type(body) == NODE_FBODY) { /* was alias */
	def = body->nd_mid;
	origin = body->nd_orig;
	body = body->nd_head;
    }

    rb_clear_cache_by_id(name);
    st_insert(RCLASS(klass)->m_tbl, name,
      (st_data_t)NEW_METHOD(NEW_FBODY(body, def, origin), orig->nd_noex));
    if (singleton) {
	rb_funcall(singleton, singleton_added, 1, ID2SYM(name));
    }
    else {
	rb_funcall(klass, added, 1, ID2SYM(name));
    }
}

static VALUE
rb_mod_alias_method(mod, newname, oldname)
    VALUE mod, newname, oldname;
{
    rb_alias(mod, rb_to_id(newname), rb_to_id(oldname));
    return mod;
}

static NODE*
copy_node_scope(node, rval)
    NODE *node;
    NODE *rval;
{
    NODE *copy = NEW_NODE(NODE_SCOPE,0,rval,node->nd_next);

    if (node->nd_tbl) {
	copy->nd_tbl = ALLOC_N(ID, node->nd_tbl[0]+1);
	MEMCPY(copy->nd_tbl, node->nd_tbl, ID, node->nd_tbl[0]+1);
    }
    else {
	copy->nd_tbl = 0;
    }
    return copy;
}

#ifdef C_ALLOCA
# define TMP_PROTECT NODE * volatile tmp__protect_tmp=0
# define TMP_ALLOC(n)							\
    (tmp__protect_tmp = rb_node_newnode(NODE_ALLOCA,			\
			     ALLOC_N(VALUE,n),tmp__protect_tmp,n),	\
     (void*)tmp__protect_tmp->nd_head)
#else
# define TMP_PROTECT typedef int foobazzz
# define TMP_ALLOC(n) ALLOCA_N(VALUE,n)
#endif

#define SETUP_ARGS0(anode,alen) do {\
    NODE *n = anode;\
    if (!n) {\
	argc = 0;\
	argv = 0;\
    }\
    else if (nd_type(n) == NODE_ARRAY) {\
	argc=alen;\
        if (argc > 0) {\
            int i;\
	    n = anode;\
	    argv = TMP_ALLOC(argc);\
	    for (i=0;i<argc;i++) {\
		argv[i] = rb_eval(self,n->nd_head);\
		n=n->nd_next;\
	    }\
        }\
        else {\
	    argc = 0;\
	    argv = 0;\
        }\
    }\
    else {\
        VALUE args = rb_eval(self,n);\
	if (TYPE(args) != T_ARRAY)\
	    args = rb_ary_to_ary(args);\
        argc = RARRAY(args)->len;\
	argv = ALLOCA_N(VALUE, argc);\
	MEMCPY(argv, RARRAY(args)->ptr, VALUE, argc);\
    }\
} while (0)

#define SETUP_ARGS(anode) SETUP_ARGS0(anode, anode->nd_alen)

#define BEGIN_CALLARGS do {\
    struct BLOCK *tmp_block = ruby_block;\
    if (ruby_iter->iter == ITER_PRE) {\
	ruby_block = ruby_block->outer;\
    }\
    PUSH_ITER(ITER_NOT)

#define END_CALLARGS \
    ruby_block = tmp_block;\
    POP_ITER();\
} while (0)

#define MATCH_DATA *rb_svar(node->nd_cnt)

static char* is_defined _((VALUE, NODE*, char*));

static char*
arg_defined(self, node, buf, type)
    VALUE self;
    NODE *node;
    char *buf;
    char *type;
{
    int argc;
    int i;

    if (!node) return type;	/* no args */
    if (nd_type(node) == NODE_ARRAY) {
	argc=node->nd_alen;
        if (argc > 0) {
	    for (i=0;i<argc;i++) {
		if (!is_defined(self, node->nd_head, buf))
		    return 0;
		node = node->nd_next;
	    }
        }
    }
    else if (!is_defined(self, node, buf)) {
	return 0;
    }
    return type;
}
    
static char*
is_defined(self, node, buf)
    VALUE self;
    NODE *node;			/* OK */
    char *buf;
{
    VALUE val;			/* OK */
    int state;

  again:
    if (!node) return "expression";
    switch (nd_type(node)) {
      case NODE_SUPER:
      case NODE_ZSUPER:
	if (ruby_frame->orig_func == 0) return 0;
	else if (ruby_frame->last_class == 0) return 0;
	else if (rb_method_boundp(RCLASS(ruby_frame->last_class)->super,
				  ruby_frame->orig_func, 0)) {
	    if (nd_type(node) == NODE_SUPER) {
		return arg_defined(self, node->nd_args, buf, "super");
	    }
	    return "super";
	}
	break;

      case NODE_VCALL:
      case NODE_FCALL:
	val = self;
	goto check_bound;

      case NODE_ATTRASGN:
	val = self;
	if (node->nd_recv == (NODE *)1) goto check_bound;
      case NODE_CALL:
	PUSH_TAG(PROT_NONE);
	if ((state = EXEC_TAG()) == 0) {
	    val = rb_eval(self, node->nd_recv);
	}
	POP_TAG();
	if (state) {
	    ruby_errinfo = Qnil;
	    return 0;
	}
      check_bound:
	{
	    int call = nd_type(node)==NODE_CALL;

	    val = CLASS_OF(val);
	    if (call) {
		int noex;
		ID id = node->nd_mid;

		if (!rb_get_method_body(&val, &id, &noex))
		    break;
		if ((noex & NOEX_PRIVATE))
		    break;
		if ((noex & NOEX_PROTECTED) &&
		    !rb_obj_is_kind_of(self, rb_class_real(val)))
		    break;
	    }
	    else if (!rb_method_boundp(val, node->nd_mid, call))
		break;
	    return arg_defined(self, node->nd_args, buf,
			       nd_type(node) == NODE_ATTRASGN ?
			       "assignment" : "method");
	}
	break;

      case NODE_MATCH2:
      case NODE_MATCH3:
	return "method";

      case NODE_YIELD:
	if (rb_block_given_p()) {
	    return "yield";
	}
	break;

      case NODE_SELF:
	return "self";

      case NODE_NIL:
	return "nil";

      case NODE_TRUE:
	return "true";

      case NODE_FALSE:
	return "false";

      case NODE_ATTRSET:
      case NODE_OP_ASGN1:
      case NODE_OP_ASGN2:
      case NODE_MASGN:
      case NODE_LASGN:
      case NODE_DASGN:
      case NODE_DASGN_CURR:
      case NODE_GASGN:
      case NODE_CDECL:
      case NODE_CVDECL:
      case NODE_CVASGN:
	return "assignment";

      case NODE_LVAR:
	return "local-variable";
      case NODE_DVAR:
	return "local-variable(in-block)";

      case NODE_GVAR:
	if (rb_gvar_defined(node->nd_entry)) {
	    return "global-variable";
	}
	break;

      case NODE_IVAR:
	if (rb_ivar_defined(self, node->nd_vid)) {
	    return "instance-variable";
	}
	break;

      case NODE_CONST:
	if (ev_const_defined(RNODE(ruby_frame->cbase), node->nd_vid, self)) {
	    return "constant";
	}
	break;

      case NODE_CVAR:
	if (rb_cvar_defined(cvar_cbase(), node->nd_vid)) {
	    return "class variable";
	}
	break;

      case NODE_COLON2:
	PUSH_TAG(PROT_NONE);
	if ((state = EXEC_TAG()) == 0) {
	    val = rb_eval(self, node->nd_head);
	}
	POP_TAG();
	if (state) {
	    ruby_errinfo = Qnil;
	    return 0;
	}
	else {
	    switch (TYPE(val)) {
	      case T_CLASS:
	      case T_MODULE:
		if (rb_const_defined_from(val, node->nd_mid))
		    return "constant";
		break;
	      default:
		if (rb_method_boundp(CLASS_OF(val), node->nd_mid, 1)) {
		    return "method";
		}
	    }
	}
	break;

      case NODE_NTH_REF:
	if (RTEST(rb_reg_nth_defined(node->nd_nth, MATCH_DATA))) {
	    sprintf(buf, "$%d", (int)node->nd_nth);
	    return buf;
	}
	break;

      case NODE_BACK_REF:
	if (RTEST(rb_reg_nth_defined(0, MATCH_DATA))) {
	    sprintf(buf, "$%c", (char)node->nd_nth);
	    return buf;
	}
	break;

      case NODE_NEWLINE:
	node = node->nd_next;
	goto again;

      default:
	PUSH_TAG(PROT_NONE);
	if ((state = EXEC_TAG()) == 0) {
	    rb_eval(self, node);
	}
	POP_TAG();
	if (!state) {
	    return "expression";
	}
	ruby_errinfo = Qnil;
	break;
    }
    return 0;
}

static int handle_rescue _((VALUE,NODE*));

static void blk_free();

static VALUE
rb_obj_is_proc(proc)
    VALUE proc;
{
    if (TYPE(proc) == T_DATA && RDATA(proc)->dfree == (RUBY_DATA_FUNC)blk_free) {
	return Qtrue;
    }
    return Qfalse;
}

static VALUE
set_trace_func(obj, trace)
    VALUE obj, trace;
{
    if (NIL_P(trace)) {
	trace_func = 0;
	return Qnil;
    }
    if (!rb_obj_is_proc(trace)) {
	rb_raise(rb_eTypeError, "trace_func needs to be Proc");
    }
    return trace_func = trace;
}

static void
call_trace_func(event, node, self, id, klass)
    char *event;
    NODE *node;
    VALUE self;
    ID id;
    VALUE klass;		/* OK */
{
    int state, raised;
    struct FRAME *prev;
    NODE *node_save;
    VALUE srcfile;

    if (!trace_func) return;
    if (tracing) return;
    if (ruby_in_compile) return;
    if (id == ID_ALLOCATOR) return;

    if (!(node_save = ruby_current_node)) {
	node_save = NEW_NEWLINE(0);
    }
    tracing = 1;
    prev = ruby_frame;
    PUSH_FRAME();
    *ruby_frame = *prev;
    ruby_frame->prev = prev;
    ruby_frame->iter = 0;	/* blocks not available anyway */

    if (node) {
	ruby_current_node = node;
	ruby_frame->node = node;
	ruby_sourcefile = node->nd_file;
	ruby_sourceline = nd_line(node);
    }
    if (klass) {
	if (TYPE(klass) == T_ICLASS) {
	    klass = RBASIC(klass)->klass;
	}
	else if (FL_TEST(klass, FL_SINGLETON)) {
	    klass = self;
	}
    }
    PUSH_TAG(PROT_NONE);
    raised = thread_reset_raised();
    if ((state = EXEC_TAG()) == 0) {
	srcfile = rb_str_new2(ruby_sourcefile?ruby_sourcefile:"(ruby)");
	proc_invoke(trace_func, rb_ary_new3(6, rb_str_new2(event),
					    srcfile,
					    INT2FIX(ruby_sourceline),
					    id?ID2SYM(id):Qnil,
					    self?rb_f_binding(self):Qnil,
					    klass),
		    Qundef, 0);
    }
    if (raised) thread_set_raised();
    POP_TMPTAG();		/* do not propagate retval */
    POP_FRAME();

    tracing = 0;
    ruby_current_node = node_save;
    SET_CURRENT_SOURCE();
    if (state) JUMP_TAG(state);
}

static VALUE
avalue_to_svalue(v)
    VALUE v;
{
    VALUE tmp, top;

    tmp = rb_check_array_type(v);
    if (NIL_P(tmp)) {
	return v;
    }
    if (RARRAY(tmp)->len == 0) {
	return Qundef;
    }
    if (RARRAY(tmp)->len == 1) {
	top = rb_check_array_type(RARRAY(tmp)->ptr[0]);
	if (NIL_P(top)) {
	    return RARRAY(tmp)->ptr[0];
	}
	if (RARRAY(top)->len > 1) {
	    return v;
	}
	return top;
    }
    return tmp;
}

static VALUE
svalue_to_avalue(v)
    VALUE v;
{
    VALUE tmp, top;

    if (v == Qundef) return rb_ary_new2(0);
    tmp = rb_check_array_type(v);
    if (NIL_P(tmp)) {
	return rb_ary_new3(1, v);
    }
    if (RARRAY(tmp)->len == 1) {
	top = rb_check_array_type(RARRAY(tmp)->ptr[0]);
	if (!NIL_P(top) && RARRAY(top)->len > 1) {
	    return v;
	}
	return rb_ary_new3(1, v);
    }
    return tmp;
}

static VALUE
svalue_to_mrhs(v, lhs)
    VALUE v;
    NODE *lhs;
{
    VALUE tmp;

    if (v == Qundef) return rb_ary_new2(0);
    tmp = rb_check_array_type(v);
    if (NIL_P(tmp)) {
	return rb_ary_new3(1, v);
    }
    /* no lhs means splat lhs only */
    if (!lhs && RARRAY(tmp)->len <= 1) {
	return rb_ary_new3(1, v);
    }
    return tmp;
}

static VALUE
avalue_splat(v)
    VALUE v;
{
    if (RARRAY(v)->len == 0) {
	return Qundef;
    }
    if (RARRAY(v)->len == 1) {
	return RARRAY(v)->ptr[0];
    }
    return v;
}

#if 1
VALUE
rb_Array(val)
    VALUE val;
{
    VALUE tmp = rb_check_array_type(val);

    if (NIL_P(tmp)) {
	/* hack to avoid invoke Object#to_a */
	VALUE origin;
	ID id = rb_intern("to_a");

	if (search_method(CLASS_OF(val), id, &origin) &&
	    RCLASS(origin)->m_tbl != RCLASS(rb_mKernel)->m_tbl) { /* exclude Kernel#to_a */
	    val = rb_funcall(val, id, 0);
	    if (TYPE(val) != T_ARRAY) {
		rb_raise(rb_eTypeError, "`to_a' did not return Array");
	    }
	    return val;
	}
	else {
	    return rb_ary_new3(1, val);
	}
    }
    return tmp;
}
#endif

static VALUE
splat_value(v)
    VALUE v;
{
	if (NIL_P(v)) return rb_ary_new3(1, Qnil);
	return rb_Array(v);
}

static VALUE
class_prefix(self, cpath)
    VALUE self;
    NODE *cpath;
{
    if (!cpath) {
	rb_bug("class path missing");
    }
    if (cpath->nd_head) {
	VALUE c = rb_eval(self, cpath->nd_head);
	switch (TYPE(c)) {
	  case T_CLASS:
	  case T_MODULE:
	    break;
	  default:
	    rb_raise(rb_eTypeError, "%s is not a class/module",
		     RSTRING(rb_obj_as_string(c))->ptr);
	}
	return c;
    }
    else if (nd_type(cpath) == NODE_COLON2) {
	return ruby_cbase;
    }
    else if (ruby_wrapper) {
	return ruby_wrapper;
    }
    else {
	return rb_cObject;
    }
}

static void return_check _((void));
#define return_value(v) do {\
  if ((prot_tag->retval = (v)) == Qundef) {\
    prot_tag->retval = Qnil;\
  }\
} while (0)

static VALUE
rb_eval(self, n)
    VALUE self;
    NODE *n;
{
    NODE * volatile contnode = 0;
    NODE * volatile node = n;
    int state;
    volatile VALUE result = Qnil;

#define RETURN(v) do { \
    result = (v); \
    goto finish; \
} while (0)

  again:
    if (!node) RETURN(Qnil);

    ruby_current_node = node;
    switch (nd_type(node)) {
      case NODE_BLOCK:
	if (contnode) {
	    result = rb_eval(self, node);
	    break;
	}
	contnode = node->nd_next;
	node = node->nd_head;
	goto again;

      case NODE_POSTEXE:
	rb_f_END();
	nd_set_type(node, NODE_NIL); /* exec just once */
	result = Qnil;
	break;

	/* begin .. end without clauses */
      case NODE_BEGIN:
	node = node->nd_body;
	goto again;

	/* nodes for speed-up(default match) */
      case NODE_MATCH:
	result = rb_reg_match2(node->nd_lit);
	break;

	/* nodes for speed-up(literal match) */
      case NODE_MATCH2:
	result = rb_reg_match(rb_eval(self,node->nd_recv),
			      rb_eval(self,node->nd_value));
	break;

	/* nodes for speed-up(literal match) */
      case NODE_MATCH3:
        {
	    VALUE r = rb_eval(self,node->nd_recv);
	    VALUE l = rb_eval(self,node->nd_value);
	    if (TYPE(l) == T_STRING) {
		result = rb_reg_match(r, l);
	    }
	    else {
		result = rb_funcall(l, match, 1, r);
	    }
	}
	break;

	/* node for speed-up(top-level loop for -n/-p) */
      case NODE_OPT_N:
	PUSH_TAG(PROT_NONE);
	switch (state = EXEC_TAG()) {
	  case 0:
	  opt_n_next:
	    while (!NIL_P(rb_gets())) {
	      opt_n_redo:
		rb_eval(self, node->nd_body);
	    }
	    break;

	  case TAG_REDO:
	    state = 0;
	    goto opt_n_redo;
	  case TAG_NEXT:
	    state = 0;
	    goto opt_n_next;
	  case TAG_BREAK:
	    state = 0;
	  default:
	    break;
	}
	POP_TAG();
	if (state) JUMP_TAG(state);
	RETURN(Qnil);

      case NODE_SELF:
	RETURN(self);

      case NODE_NIL:
	RETURN(Qnil);

      case NODE_TRUE:
	RETURN(Qtrue);

      case NODE_FALSE:
	RETURN(Qfalse);

      case NODE_IF:
	if (trace_func) {
	    call_trace_func("line", node, self,
			    ruby_frame->last_func,
			    ruby_frame->last_class);
	}
	if (RTEST(rb_eval(self, node->nd_cond))) {
	    node = node->nd_body;
	}
	else {
	    node = node->nd_else;
	}
	goto again;

      case NODE_WHEN:
	while (node) {
	    NODE *tag;

	    if (nd_type(node) != NODE_WHEN) goto again;
	    tag = node->nd_head;
	    while (tag) {
		if (trace_func) {
		    call_trace_func("line", tag, self,
				    ruby_frame->last_func,
				    ruby_frame->last_class);	
		}
		if (nd_type(tag->nd_head) == NODE_WHEN) {
		    VALUE v = rb_eval(self, tag->nd_head->nd_head);
		    long i;

		    if (TYPE(v) != T_ARRAY) v = rb_ary_to_ary(v);
		    for (i=0; i<RARRAY(v)->len; i++) {
			if (RTEST(RARRAY(v)->ptr[i])) {
			    node = node->nd_body;
			    goto again;
			}
		    }
		    tag = tag->nd_next;
		    continue;
		}
		if (RTEST(rb_eval(self, tag->nd_head))) {
		    node = node->nd_body;
		    goto again;
		}
		tag = tag->nd_next;
	    }
	    node = node->nd_next;
	}
	RETURN(Qnil);

      case NODE_CASE:
	{
	    VALUE val;

	    val = rb_eval(self, node->nd_head);
	    node = node->nd_body;
	    while (node) {
		NODE *tag;

		if (nd_type(node) != NODE_WHEN) {
		    goto again;
		}
		tag = node->nd_head;
		while (tag) {
		    if (trace_func) {
			call_trace_func("line", tag, self,
					ruby_frame->last_func,
					ruby_frame->last_class);	
		    }
		    if (nd_type(tag->nd_head) == NODE_WHEN) {
			VALUE v = rb_eval(self, tag->nd_head->nd_head);
			long i;

			if (TYPE(v) != T_ARRAY) v = rb_ary_to_ary(v);
			for (i=0; i<RARRAY(v)->len; i++) {
			    if (RTEST(rb_funcall2(RARRAY(v)->ptr[i], eqq, 1, &val))){
				node = node->nd_body;
				goto again;
			    }
			}
			tag = tag->nd_next;
			continue;
		    }
		    if (RTEST(rb_funcall2(rb_eval(self, tag->nd_head), eqq, 1, &val))) {
			node = node->nd_body;
			goto again;
		    }
		    tag = tag->nd_next;
		}
		node = node->nd_next;
	    }
	}
	RETURN(Qnil);

      case NODE_WHILE:
	PUSH_TAG(PROT_NONE);
	result = Qnil;
	switch (state = EXEC_TAG()) {
	  case 0:
	    if (node->nd_state && !RTEST(rb_eval(self, node->nd_cond)))
		goto while_out;
	    do {
	      while_redo:
		rb_eval(self, node->nd_body);
	      while_next:
		;
	    } while (RTEST(rb_eval(self, node->nd_cond)));
	    break;

	  case TAG_REDO:
	    state = 0;
	    goto while_redo;
	  case TAG_NEXT:
	    state = 0;
	    goto while_next;
	  case TAG_BREAK:
	    state = 0;
	    result = prot_tag->retval;
	  default:
	    break;
	}
      while_out:
	POP_TAG();
	if (state) JUMP_TAG(state);
	RETURN(result);

      case NODE_UNTIL:
	PUSH_TAG(PROT_NONE);
	result = Qnil;
	switch (state = EXEC_TAG()) {
	  case 0:
	    if (node->nd_state && RTEST(rb_eval(self, node->nd_cond)))
		goto until_out;
	    do {
	      until_redo:
		rb_eval(self, node->nd_body);
	      until_next:
		;
	    } while (!RTEST(rb_eval(self, node->nd_cond)));
	    break;

	  case TAG_REDO:
	    state = 0;
	    goto until_redo;
	  case TAG_NEXT:
	    state = 0;
	    goto until_next;
	  case TAG_BREAK:
	    state = 0;
	    result = prot_tag->retval;
	  default:
	    break;
	}
      until_out:
	POP_TAG();
	if (state) JUMP_TAG(state);
	RETURN(result);

      case NODE_BLOCK_PASS:
	result = block_pass(self, node);
	break;

      case NODE_ITER:
      case NODE_FOR:
	{
	  iter_retry:
	    PUSH_TAG(PROT_FUNC);
	    PUSH_BLOCK(node->nd_var, node->nd_body);

	    state = EXEC_TAG();
	    if (state == 0) {
		PUSH_ITER(ITER_PRE);
		if (nd_type(node) == NODE_ITER) {
		    result = rb_eval(self, node->nd_iter);
		}
		else {
		    VALUE recv;

		    _block.flags &= ~BLOCK_D_SCOPE;
		    BEGIN_CALLARGS;
		    recv = rb_eval(self, node->nd_iter);
		    END_CALLARGS;
		    ruby_current_node = node;
		    SET_CURRENT_SOURCE();
		    result = rb_call(CLASS_OF(recv),recv,each,0,0,0);
		}
		POP_ITER();
	    }
	    else if (_block.tag->dst == state) {
		state &= TAG_MASK;
		if (state == TAG_RETURN || state == TAG_BREAK) {
		    result = prot_tag->retval;
		}
	    }
	    POP_BLOCK();
	    POP_TAG();
	    switch (state) {
	      case 0:
		break;

	      case TAG_RETRY:
		goto iter_retry;

	      case TAG_BREAK:
		break;

	      case TAG_RETURN:
		return_value(result);
		/* fall through */
	      default:
		JUMP_TAG(state);
	    }
	}
	break;

      case NODE_BREAK:
	return_value(rb_eval(self, node->nd_stts));
	JUMP_TAG(TAG_BREAK);
	break;

      case NODE_NEXT:
	CHECK_INTS;
	return_value(rb_eval(self, node->nd_stts));
	JUMP_TAG(TAG_NEXT);
	break;

      case NODE_REDO:
	CHECK_INTS;
	JUMP_TAG(TAG_REDO);
	break;

      case NODE_RETRY:
	CHECK_INTS;
	JUMP_TAG(TAG_RETRY);
	break;

      case NODE_RESTARY:
      case NODE_RESTARY2:
	result = splat_value(rb_eval(self, node->nd_head));
	break;

      case NODE_SPLAT:
	result = avalue_splat(splat_value(rb_eval(self, node->nd_head)));
	break;

      case NODE_SVALUE:
	result = rb_eval(self, node->nd_head);
	if (result == Qundef) result = Qnil;
	break;

      case NODE_YIELD:
	if (node->nd_head) {
	    result = rb_eval(self, node->nd_head);
	}
	else {
	    result = Qundef;	/* no arg */
	}
	SET_CURRENT_SOURCE();
	result = rb_yield_0(result, 0, 0, 0, node->nd_state);
	break;

      case NODE_RESCUE:
      retry_entry:
        {
	    volatile VALUE e_info = ruby_errinfo;

	    PUSH_TAG(PROT_NONE);
	    if ((state = EXEC_TAG()) == 0) {
		result = rb_eval(self, node->nd_head);
	    }
	    POP_TAG();
	    if (state == TAG_RAISE) {
		NODE * volatile resq = node->nd_resq;

		while (resq) {
		    ruby_current_node = resq;
		    if (handle_rescue(self, resq)) {
			state = 0;
			PUSH_TAG(PROT_NONE);
			if ((state = EXEC_TAG()) == 0) {
			    result = rb_eval(self, resq->nd_body);
			}
			POP_TAG();
			if (state == TAG_RETRY) {
			    state = 0;
			    ruby_errinfo = Qnil;
			    goto retry_entry;
			}
			if (state != TAG_RAISE) {
			    ruby_errinfo = e_info;
			}
			break;
		    }
		    resq = resq->nd_head; /* next rescue */
		}
	    }
	    else if (node->nd_else) { /* else clause given */
		if (!state) {	/* no exception raised */
		    result = rb_eval(self, node->nd_else);
		}
	    }
	    if (state) JUMP_TAG(state);
	}
        break;

      case NODE_ENSURE:
	PUSH_TAG(PROT_NONE);
	if ((state = EXEC_TAG()) == 0) {
	    result = rb_eval(self, node->nd_head);
	}
	POP_TAG();
	if (node->nd_ensr) {
	    VALUE retval = prot_tag->retval; /* save retval */
	    VALUE errinfo = ruby_errinfo;

	    rb_eval(self, node->nd_ensr);
	    return_value(retval);
	    ruby_errinfo = errinfo;
	}
	if (state) JUMP_TAG(state);
	break;

      case NODE_AND:
	result = rb_eval(self, node->nd_1st);
	if (!RTEST(result)) break;
	node = node->nd_2nd;
	goto again;

      case NODE_OR:
	result = rb_eval(self, node->nd_1st);
	if (RTEST(result)) break;
	node = node->nd_2nd;
	goto again;

      case NODE_NOT:
	if (RTEST(rb_eval(self, node->nd_body))) result = Qfalse;
	else result = Qtrue;
	break;

      case NODE_DOT2:
      case NODE_DOT3:
	result = rb_range_new(rb_eval(self, node->nd_beg),
			      rb_eval(self, node->nd_end),
			      nd_type(node) == NODE_DOT3);
	if (node->nd_state) break;
	if (nd_type(node->nd_beg) == NODE_LIT && FIXNUM_P(node->nd_beg->nd_lit) &&
	    nd_type(node->nd_end) == NODE_LIT && FIXNUM_P(node->nd_end->nd_lit))
	{
	    nd_set_type(node, NODE_LIT);
	    node->nd_lit = result;
	}
	else {
	    node->nd_state = 1;
	}
	break;

      case NODE_FLIP2:		/* like AWK */
	{
	    VALUE *flip = rb_svar(node->nd_cnt);
	    if (!flip) rb_bug("unexpected local variable");
	    if (!RTEST(*flip)) {
		if (RTEST(rb_eval(self, node->nd_beg))) {
		    *flip = RTEST(rb_eval(self, node->nd_end))?Qfalse:Qtrue;
		    result = Qtrue;
		}
		else {
		    result = Qfalse;
		}
	    }
	    else {
		if (RTEST(rb_eval(self, node->nd_end))) {
		    *flip = Qfalse;
		}
		result = Qtrue;
	    }
	}
	break;

      case NODE_FLIP3:		/* like SED */
	{
	    VALUE *flip = rb_svar(node->nd_cnt);
	    if (!flip) rb_bug("unexpected local variable");
	    if (!RTEST(*flip)) {
		result = RTEST(rb_eval(self, node->nd_beg)) ? Qtrue : Qfalse;
		*flip = result;
	    }
	    else {
		if (RTEST(rb_eval(self, node->nd_end))) {
		    *flip = Qfalse;
		}
		result = Qtrue;
	    }
	}
	break;

      case NODE_RETURN:
	return_value(rb_eval(self, node->nd_stts));
	return_check();
	JUMP_TAG(TAG_RETURN);
	break;

      case NODE_ARGSCAT:
	result = rb_ary_concat(rb_eval(self, node->nd_head),
			       splat_value(rb_eval(self, node->nd_body)));
	break;

      case NODE_ARGSPUSH:
	result = rb_ary_push(rb_ary_dup(rb_eval(self, node->nd_head)),
			     rb_eval(self, node->nd_body));
	break;

      case NODE_ATTRASGN:
	{
	    VALUE recv;
	    int argc; VALUE *argv; /* used in SETUP_ARGS */
	    int scope;
	    TMP_PROTECT;

	    BEGIN_CALLARGS;
	    if (node->nd_recv == (NODE *)1) {
		recv = self;
		scope = 1;
	    }
	    else {
		recv = rb_eval(self, node->nd_recv);
		scope = 0;
	    }
	    SETUP_ARGS(node->nd_args);
	    END_CALLARGS;

	    SET_CURRENT_SOURCE();
	    rb_call(CLASS_OF(recv),recv,node->nd_mid,argc,argv,scope);
	    result = argv[argc-1];
	}
	break;

      case NODE_CALL:
	{
	    VALUE recv;
	    int argc; VALUE *argv; /* used in SETUP_ARGS */
	    TMP_PROTECT;

	    BEGIN_CALLARGS;
	    recv = rb_eval(self, node->nd_recv);
	    SETUP_ARGS(node->nd_args);
	    END_CALLARGS;

	    SET_CURRENT_SOURCE();
	    result = rb_call(CLASS_OF(recv),recv,node->nd_mid,argc,argv,0);
	}
	break;

      case NODE_FCALL:
	{
	    int argc; VALUE *argv; /* used in SETUP_ARGS */
	    TMP_PROTECT;

	    BEGIN_CALLARGS;
	    SETUP_ARGS(node->nd_args);
	    END_CALLARGS;

	    SET_CURRENT_SOURCE();
	    result = rb_call(CLASS_OF(self),self,node->nd_mid,argc,argv,1);
	}
	break;

      case NODE_VCALL:
	SET_CURRENT_SOURCE();
	result = rb_call(CLASS_OF(self),self,node->nd_mid,0,0,2);
	break;

      case NODE_SUPER:
      case NODE_ZSUPER:
	{
	    int argc; VALUE *argv; /* used in SETUP_ARGS */
	    TMP_PROTECT;

	    if (ruby_frame->last_class == 0) {	
		if (ruby_frame->orig_func) {
		    rb_name_error(ruby_frame->last_func,
				  "superclass method `%s' disabled",
				  rb_id2name(ruby_frame->orig_func));
		}
		else {
		    rb_raise(rb_eNoMethodError, "super called outside of method");
		}
	    }
	    if (nd_type(node) == NODE_ZSUPER) {
		argc = ruby_frame->argc;
		argv = ruby_frame->argv;
	    }
	    else {
		BEGIN_CALLARGS;
		SETUP_ARGS(node->nd_args);
		END_CALLARGS;
	    }

	    SET_CURRENT_SOURCE();
	    result = rb_call_super(argc, argv);
	}
	break;

      case NODE_SCOPE:
	{
	    struct FRAME frame;
	    NODE *saved_cref = 0;

	    frame = *ruby_frame;
	    frame.tmp = ruby_frame;
	    ruby_frame = &frame;

	    PUSH_SCOPE();
	    PUSH_TAG(PROT_NONE);
	    if (node->nd_rval) {
		saved_cref = ruby_cref;
		ruby_cref = (NODE*)node->nd_rval;
		ruby_frame->cbase = node->nd_rval;
	    }
	    if (node->nd_tbl) {
		VALUE *vars = ALLOCA_N(VALUE, node->nd_tbl[0]+1);
		*vars++ = (VALUE)node;
		ruby_scope->local_vars = vars;
		rb_mem_clear(ruby_scope->local_vars, node->nd_tbl[0]);
		ruby_scope->local_tbl = node->nd_tbl;
	    }
	    else {
		ruby_scope->local_vars = 0;
		ruby_scope->local_tbl  = 0;
	    }
	    if ((state = EXEC_TAG()) == 0) {
		result = rb_eval(self, node->nd_next);
	    }
	    POP_TAG();
	    POP_SCOPE();
	    ruby_frame = frame.tmp;
	    if (saved_cref)
		ruby_cref = saved_cref;
	    if (state) JUMP_TAG(state);
	}
	break;

      case NODE_OP_ASGN1:
	{
	    int argc; VALUE *argv; /* used in SETUP_ARGS */
	    VALUE recv, val;
	    NODE *rval;
	    TMP_PROTECT;

	    recv = rb_eval(self, node->nd_recv);
	    rval = node->nd_args->nd_head;
	    SETUP_ARGS0(node->nd_args->nd_next, node->nd_args->nd_alen - 1);
	    val = rb_funcall2(recv, aref, argc-1, argv);
	    switch (node->nd_mid) {
	    case 0: /* OR */
		if (RTEST(val)) RETURN(val);
		val = rb_eval(self, rval);
		break;
	    case 1: /* AND */
		if (!RTEST(val)) RETURN(val);
		val = rb_eval(self, rval);
		break;
	    default:
		val = rb_funcall(val, node->nd_mid, 1, rb_eval(self, rval));
	    }
	    argv[argc-1] = val;
	    rb_funcall2(recv, aset, argc, argv);
	    result = val;
	}
	break;

      case NODE_OP_ASGN2:
	{
	    ID id = node->nd_next->nd_vid;
	    VALUE recv, val;

	    recv = rb_eval(self, node->nd_recv);
	    val = rb_funcall(recv, id, 0);
	    switch (node->nd_next->nd_mid) {
	    case 0: /* OR */
		if (RTEST(val)) RETURN(val);
		val = rb_eval(self, node->nd_value);
		break;
	    case 1: /* AND */
		if (!RTEST(val)) RETURN(val);
		val = rb_eval(self, node->nd_value);
		break;
	    default:
		val = rb_funcall(val, node->nd_next->nd_mid, 1,
				 rb_eval(self, node->nd_value));
	    }

	    rb_funcall2(recv, node->nd_next->nd_aid, 1, &val);
	    result = val;
	}
	break;

      case NODE_OP_ASGN_AND:
	result = rb_eval(self, node->nd_head);
	if (!RTEST(result)) break;
	node = node->nd_value;
	goto again;

      case NODE_OP_ASGN_OR:
	if ((node->nd_aid && !is_defined(self, node->nd_head, 0)) ||
	    !RTEST(result = rb_eval(self, node->nd_head))) {
	    node = node->nd_value;
	    goto again;
	}
	break;

      case NODE_MASGN:
	result = svalue_to_mrhs(rb_eval(self, node->nd_value), node->nd_head);
	result = massign(self, node, result, 0);
	break;

      case NODE_LASGN:
	if (ruby_scope->local_vars == 0)
	    rb_bug("unexpected local variable assignment");
	result = rb_eval(self, node->nd_value);
	ruby_scope->local_vars[node->nd_cnt] = result;
	break;

      case NODE_DASGN:
	result = rb_eval(self, node->nd_value);
	dvar_asgn(node->nd_vid, result);
	break;

      case NODE_DASGN_CURR:
	result = rb_eval(self, node->nd_value);
	dvar_asgn_curr(node->nd_vid, result);
	break;

      case NODE_GASGN:
	result = rb_eval(self, node->nd_value);
	rb_gvar_set(node->nd_entry, result);
	break;

      case NODE_IASGN:
	result = rb_eval(self, node->nd_value);
	rb_ivar_set(self, node->nd_vid, result);
	break;

      case NODE_CDECL:
	result = rb_eval(self, node->nd_value);
	if (node->nd_vid == 0) {
	    rb_const_set(class_prefix(self, node->nd_else), node->nd_else->nd_mid, result);
	}
	else {
	    if (NIL_P(ruby_cbase)) {
		rb_raise(rb_eTypeError, "no class/module to define constant");
	    }
	    rb_const_set(ruby_cbase, node->nd_vid, result);
	}
	break;

      case NODE_CVDECL:
	if (NIL_P(ruby_cbase)) {
	    rb_raise(rb_eTypeError, "no class/module to define class variable");
	}
	result = rb_eval(self, node->nd_value);
	rb_cvar_set(cvar_cbase(), node->nd_vid, result, Qtrue);
	break;

      case NODE_CVASGN:
	result = rb_eval(self, node->nd_value);
	rb_cvar_set(cvar_cbase(), node->nd_vid, result, Qfalse);
	break;

      case NODE_LVAR:
	if (ruby_scope->local_vars == 0) {
	    rb_bug("unexpected local variable");
	}
	result = ruby_scope->local_vars[node->nd_cnt];
	break;

      case NODE_DVAR:
	result = rb_dvar_ref(node->nd_vid);
	break;

      case NODE_GVAR:
	result = rb_gvar_get(node->nd_entry);
	break;

      case NODE_IVAR:
	result = rb_ivar_get(self, node->nd_vid);
	break;

      case NODE_CONST:
	result = ev_const_get(RNODE(ruby_frame->cbase), node->nd_vid, self);
	break;

      case NODE_CVAR:
	result = rb_cvar_get(cvar_cbase(), node->nd_vid);
	break;

      case NODE_BLOCK_ARG:
	if (ruby_scope->local_vars == 0)
	    rb_bug("unexpected block argument");
	if (rb_block_given_p()) {
	    result = rb_block_proc();
	    ruby_scope->local_vars[node->nd_cnt] = result;
	}
	else {
	    result = Qnil;
	}
	break;

      case NODE_COLON2:
	{
	    VALUE klass;

	    klass = rb_eval(self, node->nd_head);
	    if (rb_is_const_id(node->nd_mid)) {
		switch (TYPE(klass)) {
		  case T_CLASS:
		  case T_MODULE:
		    result = rb_const_get_from(klass, node->nd_mid);
		    break;
		  default:
		    rb_raise(rb_eTypeError, "%s is not a class/module",
			     RSTRING(rb_obj_as_string(klass))->ptr);
		    break;
		}
	    }
	    else {
		result = rb_funcall(klass, node->nd_mid, 0, 0);
	    }
	}
	break;

      case NODE_COLON3:
	result = rb_const_get_at(rb_cObject, node->nd_mid);
	break;

      case NODE_NTH_REF:
	result = rb_reg_nth_match(node->nd_nth, MATCH_DATA);
	break;

      case NODE_BACK_REF:
	switch (node->nd_nth) {
	  case '&':
	    result = rb_reg_last_match(MATCH_DATA);
	    break;
	  case '`':
	    result = rb_reg_match_pre(MATCH_DATA);
	    break;
	  case '\'':
	    result = rb_reg_match_post(MATCH_DATA);
	    break;
	  case '+':
	    result = rb_reg_match_last(MATCH_DATA);
	    break;
	  default:
	    rb_bug("unexpected back-ref");
	}
	break;

      case NODE_HASH:
	{
	    NODE *list;
	    VALUE hash = rb_hash_new();
	    VALUE key, val;

	    list = node->nd_head;
	    while (list) {
		key = rb_eval(self, list->nd_head);
		list = list->nd_next;
		if (list == 0)
		    rb_bug("odd number list for Hash");
		val = rb_eval(self, list->nd_head);
		list = list->nd_next;
		rb_hash_aset(hash, key, val);
	    }
	    result = hash;
	}
	break;

      case NODE_ZARRAY:		/* zero length list */
	result = rb_ary_new();
	break;

      case NODE_ARRAY:
	{
	    VALUE ary;
	    long i;

	    i = node->nd_alen;
	    ary = rb_ary_new2(i);
	    for (i=0;node;node=node->nd_next) {
		RARRAY(ary)->ptr[i++] = rb_eval(self, node->nd_head);
		RARRAY(ary)->len = i;
	    }

	    result = ary;
	}
	break;

      case NODE_STR:
	result = rb_str_new3(node->nd_lit);
	break;

      case NODE_EVSTR:
	result = rb_obj_as_string(rb_eval(self, node->nd_body));
	break;

      case NODE_DSTR:
      case NODE_DXSTR:
      case NODE_DREGX:
      case NODE_DREGX_ONCE:
      case NODE_DSYM:
	{
	    VALUE str, str2;
	    NODE *list = node->nd_next;

	    str = rb_str_new3(node->nd_lit);
	    while (list) {
		if (list->nd_head) {
		    switch (nd_type(list->nd_head)) {
		      case NODE_STR:
			str2 = list->nd_head->nd_lit;
			break;
		      default:
			str2 = rb_eval(self, list->nd_head);
			break;
		    }
		    rb_str_append(str, str2);
		    OBJ_INFECT(str, str2);
		}
		list = list->nd_next;
	    }
	    switch (nd_type(node)) {
	      case NODE_DREGX:
		result = rb_reg_new(RSTRING(str)->ptr, RSTRING(str)->len,
				    node->nd_cflag);
		break;
	      case NODE_DREGX_ONCE:	/* regexp expand once */
		result = rb_reg_new(RSTRING(str)->ptr, RSTRING(str)->len,
				    node->nd_cflag);
		nd_set_type(node, NODE_LIT);
		node->nd_lit = result;
		break;
	      case NODE_DXSTR:
		result = rb_funcall(self, '`', 1, str);
		break;
	      case NODE_DSYM:
		result = rb_str_intern(str);
		break;
	      default:
		result = str;
		break;
	    }
	}
	break;

      case NODE_XSTR:
	result = rb_funcall(self, '`', 1, node->nd_lit);
	break;

      case NODE_LIT:
	result = node->nd_lit;
	break;

      case NODE_ATTRSET:
	if (ruby_frame->argc != 1)
	    rb_raise(rb_eArgError, "wrong number of arguments(%d for 1)",
		     ruby_frame->argc);
	result = rb_ivar_set(self, node->nd_vid, ruby_frame->argv[0]);
	break;

      case NODE_DEFN:
	if (node->nd_defn) {
	    NODE *body,  *defn;
	    VALUE origin;
	    int noex;

	    if (NIL_P(ruby_class)) {
		rb_raise(rb_eTypeError, "no class/module to add method");
	    }
	    if (ruby_class == rb_cObject && node->nd_mid == init) {
		rb_warn("redefining Object#initialize may cause infinite loop");
	    }
	    if (node->nd_mid == __id__ || node->nd_mid == __send__) {
		rb_warn("redefining `%s' may cause serious problem",
			rb_id2name(node->nd_mid));
	    }
	    rb_frozen_class_p(ruby_class);
	    body = search_method(ruby_class, node->nd_mid, &origin);
	    if (body){
		if (RTEST(ruby_verbose) && ruby_class == origin && body->nd_cnt == 0 && body->nd_body) {
		    rb_warning("method redefined; discarding old %s", rb_id2name(node->nd_mid));
		}
	    }

	    if (SCOPE_TEST(SCOPE_PRIVATE) || node->nd_mid == init) {
		noex = NOEX_PRIVATE;
	    }
	    else if (SCOPE_TEST(SCOPE_PROTECTED)) {
		noex = NOEX_PROTECTED;
	    }
	    else {
		noex = NOEX_PUBLIC;
	    }
	    if (body && origin == ruby_class && body->nd_body == 0) {
		noex |= NOEX_NOSUPER;
	    }

	    defn = copy_node_scope(node->nd_defn, ruby_cref);
	    rb_add_method(ruby_class, node->nd_mid, defn, noex);
	    if (scope_vmode == SCOPE_MODFUNC) {
		rb_add_method(rb_singleton_class(ruby_class),
			      node->nd_mid, defn, NOEX_PUBLIC);
	    }
	    result = Qnil;
	}
	break;

      case NODE_DEFS:
	if (node->nd_defn) {
	    VALUE recv = rb_eval(self, node->nd_recv);
	    VALUE klass;
	    NODE *body = 0, *defn;

	    if (ruby_safe_level >= 4 && !OBJ_TAINTED(recv)) {
		rb_raise(rb_eSecurityError, "Insecure: can't define singleton method");
	    }
	    if (FIXNUM_P(recv) || SYMBOL_P(recv)) {
		rb_raise(rb_eTypeError,
			 "can't define singleton method \"%s\" for %s",
			 rb_id2name(node->nd_mid),
			 rb_obj_classname(recv));
	    }

	    if (OBJ_FROZEN(recv)) rb_error_frozen("object");
	    klass = rb_singleton_class(recv);
	    if (st_lookup(RCLASS(klass)->m_tbl, node->nd_mid, (st_data_t *)&body)) {
		if (ruby_safe_level >= 4) {
		    rb_raise(rb_eSecurityError, "redefining method prohibited");
		}
		if (RTEST(ruby_verbose)) {
		    rb_warning("redefine %s", rb_id2name(node->nd_mid));
		}
	    }
	    defn = copy_node_scope(node->nd_defn, ruby_cref);
	    defn->nd_rval = (VALUE)ruby_cref;
	    rb_add_method(klass, node->nd_mid, defn, 
			  NOEX_PUBLIC|(body?body->nd_noex&NOEX_UNDEF:0));
	    result = Qnil;
	}
	break;

      case NODE_UNDEF:
	if (NIL_P(ruby_class)) {
	    rb_raise(rb_eTypeError, "no class to undef method");
	}
	rb_undef(ruby_class, node->nd_mid);
	result = Qnil;
	break;

      case NODE_ALIAS:
	if (NIL_P(ruby_class)) {
	    rb_raise(rb_eTypeError, "no class to make alias");
	}
	rb_alias(ruby_class, node->nd_new, node->nd_old);
	result = Qnil;
	break;

      case NODE_VALIAS:
	rb_alias_variable(node->nd_new, node->nd_old);
	result = Qnil;
	break;

      case NODE_CLASS:
	{
	    VALUE super, klass, tmp, cbase;
	    ID cname;

	    if (NIL_P(ruby_cbase)) {
		rb_raise(rb_eTypeError, "no outer class/module");
	    }
	    if (node->nd_super) {
		super = superclass(self, node->nd_super);
	    }
	    else {
		super = 0;
	    }

	    cbase = class_prefix(self, node->nd_cpath);
	    cname = node->nd_cpath->nd_mid;
	    if (rb_const_defined_at(cbase, cname)) {
		klass = rb_const_get_at(cbase, cname);
		if (TYPE(klass) != T_CLASS) {
		    rb_raise(rb_eTypeError, "%s is not a class",
			     rb_id2name(cname));
		}
		if (super) {
		    tmp = rb_class_real(RCLASS(klass)->super);
		    if (tmp != super) {
			goto override_class;
		    }
		    super = 0;
		}
		if (ruby_safe_level >= 4) {
		    rb_raise(rb_eSecurityError, "extending class prohibited");
		}
	    }
	    else {
	      override_class:
		if (!super) super = rb_cObject;
		klass = rb_define_class_id(cname, super);
		rb_set_class_path(klass, cbase, rb_id2name(cname));
		rb_const_set(cbase, cname, klass);
	    }
	    if (ruby_wrapper) {
		rb_extend_object(klass, ruby_wrapper);
		rb_include_module(klass, ruby_wrapper);
	    }
	    if (super) rb_class_inherited(super, klass);
	    result = module_setup(klass, node);
	}
	break;

      case NODE_MODULE:
	{
	    VALUE module, cbase;
	    ID cname;

	    if (NIL_P(ruby_cbase)) {
		rb_raise(rb_eTypeError, "no outer class/module");
	    }
	    cbase = class_prefix(self, node->nd_cpath);
	    cname = node->nd_cpath->nd_mid;
	    if (rb_const_defined_at(cbase, cname)) {
		module = rb_const_get_at(cbase, cname);
		if (TYPE(module) != T_MODULE) {
		    rb_raise(rb_eTypeError, "%s is not a module",
			     rb_id2name(cname));
		}
		if (ruby_safe_level >= 4) {
		    rb_raise(rb_eSecurityError, "extending module prohibited");
		}
	    }
	    else {
		module = rb_define_module_id(cname);
		rb_set_class_path(module, cbase, rb_id2name(cname));
		rb_const_set(cbase, cname, module);
	    }
	    if (ruby_wrapper) {
		rb_extend_object(module, ruby_wrapper);
		rb_include_module(module, ruby_wrapper);
	    }

	    result = module_setup(module, node);
	}
	break;

      case NODE_SCLASS:
	{
	    VALUE klass;

	    result = rb_eval(self, node->nd_recv);
	    if (FIXNUM_P(result) || SYMBOL_P(result)) {
		rb_raise(rb_eTypeError, "no virtual class for %s",
			 rb_obj_classname(result));
	    }
	    if (ruby_safe_level >= 4 && !OBJ_TAINTED(result))
		rb_raise(rb_eSecurityError, "Insecure: can't extend object");
	    klass = rb_singleton_class(result);
	    
	    if (ruby_wrapper) {
		rb_extend_object(klass, ruby_wrapper);
		rb_include_module(klass, ruby_wrapper);
	    }
	    
	    result = module_setup(klass, node);
	}
	break;

      case NODE_DEFINED:
	{
	    char buf[20];
	    char *desc = is_defined(self, node->nd_head, buf);

	    if (desc) result = rb_str_new2(desc);
	    else result = Qnil;
	}
	break;

      case NODE_NEWLINE:
	if (trace_func) {
	    call_trace_func("line", node, self,
			    ruby_frame->last_func,
			    ruby_frame->last_class);	
	}
	node = node->nd_next;
	goto again;

      default:
	rb_bug("unknown node type %d", nd_type(node));
    }
  finish:
    CHECK_INTS;
    if (contnode) {
	node = contnode;
	contnode = 0;
	goto again;
    }
    return result;
}

static VALUE
module_setup(module, n)
    VALUE module;
    NODE *n;
{
    NODE * volatile node = n->nd_body;
    int state;
    struct FRAME frame;
    VALUE result;		/* OK */
    TMP_PROTECT;

    frame = *ruby_frame;
    frame.tmp = ruby_frame;
    ruby_frame = &frame;

    PUSH_CLASS();
    ruby_class = module;
    PUSH_SCOPE();
    PUSH_VARS();

    if (node->nd_tbl) {
	VALUE *vars = TMP_ALLOC(node->nd_tbl[0]+1);
	*vars++ = (VALUE)node;
	ruby_scope->local_vars = vars;
	rb_mem_clear(ruby_scope->local_vars, node->nd_tbl[0]);
	ruby_scope->local_tbl = node->nd_tbl;
    }
    else {
	ruby_scope->local_vars = 0;
	ruby_scope->local_tbl  = 0;
    }

    PUSH_CREF(module);
    ruby_frame->cbase = (VALUE)ruby_cref;
    PUSH_TAG(PROT_NONE);
    if ((state = EXEC_TAG()) == 0) {
	if (trace_func) {
	    call_trace_func("class", n, ruby_cbase, ruby_frame->last_func, ruby_frame->last_class);
	}
	result = rb_eval(ruby_cbase, node->nd_next);
    }
    POP_TAG();
    POP_CREF();
    POP_VARS();
    POP_SCOPE();
    POP_CLASS();

    ruby_frame = frame.tmp;
    if (trace_func) {
	call_trace_func("end", n, 0, ruby_frame->last_func, ruby_frame->last_class);
    }
    if (state) JUMP_TAG(state);

    return result;
}

int
rb_respond_to(obj, id)
    VALUE obj;
    ID id;
{
    if (rb_method_boundp(CLASS_OF(obj), id, 0)) {
	return Qtrue;
    }
    return Qfalse;
}

static VALUE
rb_obj_respond_to(argc, argv, obj)
    int argc;
    VALUE *argv;
    VALUE obj;
{
    VALUE mid, priv;
    ID id;

    rb_scan_args(argc, argv, "11", &mid, &priv);
    id = rb_to_id(mid);
    if (rb_method_boundp(CLASS_OF(obj), id, !RTEST(priv))) {
	return Qtrue;
    }
    return Qfalse;
}

static VALUE
rb_mod_method_defined(mod, mid)
    VALUE mod, mid;
{
    return rb_method_boundp(mod, rb_to_id(mid), 1);
}

#define VISI_CHECK(x,f) (((x)&NOEX_MASK) == (f))

static VALUE
rb_mod_public_method_defined(mod, mid)
    VALUE mod, mid;
{
    ID id = rb_to_id(mid);
    int noex;

    if (rb_get_method_body(&mod, &id, &noex)) {
	if (VISI_CHECK(noex, NOEX_PUBLIC))
	    return Qtrue;
    }
    return Qfalse;
}

static VALUE
rb_mod_private_method_defined(mod, mid)
    VALUE mod, mid;
{
    ID id = rb_to_id(mid);
    int noex;

    if (rb_get_method_body(&mod, &id, &noex)) {
	if (VISI_CHECK(noex, NOEX_PRIVATE))
	    return Qtrue;
    }
    return Qfalse;
}

static VALUE
rb_mod_protected_method_defined(mod, mid)
    VALUE mod, mid;
{
    ID id = rb_to_id(mid);
    int noex;

    if (rb_get_method_body(&mod, &id, &noex)) {
	if (VISI_CHECK(noex, NOEX_PROTECTED))
	    return Qtrue;
    }
    return Qfalse;
}

#define terminate_process(status, mesg, mlen) rb_exc_raise(system_exit(status, mesg, mlen))

static VALUE
system_exit(status, mesg, mlen)
    int status;
    const char *mesg;
    long mlen;
{
    VALUE args[2];
    args[0] = INT2NUM(status);
    args[1] = rb_str_new(mesg, mlen);

    return rb_class_new_instance(2, args, rb_eSystemExit);
}

void
rb_exit(status)
    int status;
{
    if (prot_tag) {
	terminate_process(status, "exit", 4);
    }
    ruby_finalize();
    exit(status);
}

VALUE
rb_f_exit(argc, argv)
    int argc;
    VALUE *argv;
{
    VALUE status;
    int istatus;

    rb_secure(4);
    if (rb_scan_args(argc, argv, "01", &status) == 1) {
	istatus = NUM2INT(status);
    }
    else {
	istatus = 0;
    }
    rb_exit(istatus);
    return Qnil;		/* not reached */
}

VALUE
rb_f_abort(argc, argv)
    int argc;
    VALUE *argv;
{
    rb_secure(4);
    if (argc == 0) {
	if (!NIL_P(ruby_errinfo)) {
	    error_print();
	}
	rb_exit(1);
    }
    else {
	VALUE mesg;

	rb_scan_args(argc, argv, "1", &mesg);
	StringValue(argv[0]);
	rb_io_puts(argc, argv, rb_stderr);
	terminate_process(1, RSTRING(argv[0])->ptr, RSTRING(argv[0])->len);
    }
    return Qnil;		/* not reached */
}

void
rb_iter_break()
{
    JUMP_TAG(TAG_BREAK);
}

NORETURN(static void rb_longjmp _((int, VALUE)));
static VALUE make_backtrace _((void));

static void
rb_longjmp(tag, mesg)
    int tag;
    VALUE mesg;
{
    VALUE at;

    if (thread_set_raised()) {
	ruby_errinfo = exception_error;
	JUMP_TAG(TAG_FATAL);
    }
    if (NIL_P(mesg)) mesg = ruby_errinfo;
    if (NIL_P(mesg)) {
	mesg = rb_exc_new(rb_eRuntimeError, 0, 0);
    }

    ruby_set_current_source();
    if (ruby_sourcefile && !NIL_P(mesg)) {
	at = get_backtrace(mesg);
	if (NIL_P(at)) {
	    at = make_backtrace();
	    set_backtrace(mesg, at);
	}
    }
    if (!NIL_P(mesg)) {
	ruby_errinfo = mesg;
    }

    if (RTEST(ruby_debug) && !NIL_P(ruby_errinfo)
	&& !rb_obj_is_kind_of(ruby_errinfo, rb_eSystemExit)) {
	VALUE e = ruby_errinfo;
	int status;

	PUSH_TAG(PROT_NONE);
	if ((status = EXEC_TAG()) == 0) {
	    StringValue(e);
	    warn_printf("Exception `%s' at %s:%d - %s\n",
			rb_obj_classname(ruby_errinfo),
			ruby_sourcefile, ruby_sourceline,
			RSTRING(e)->ptr);
	}
	POP_TAG();
	if (status) {
	    thread_reset_raised();
	    JUMP_TAG(status);
	}
    }

    rb_trap_restore_mask();
    if (trace_func && tag != TAG_FATAL) {
	call_trace_func("raise", ruby_current_node,
			ruby_frame->self,
			ruby_frame->last_func,
			ruby_frame->last_class);
    }
    if (!prot_tag) {
	error_print();
    }
    thread_reset_raised();
    JUMP_TAG(tag);
}

void
rb_exc_raise(mesg)
    VALUE mesg;
{
    rb_longjmp(TAG_RAISE, mesg);
}

void
rb_exc_fatal(mesg)
    VALUE mesg;
{
    rb_longjmp(TAG_FATAL, mesg);
}

void
rb_interrupt()
{
    rb_raise(rb_eInterrupt, "");
}

static VALUE
rb_f_raise(argc, argv)
    int argc;
    VALUE *argv;
{
    VALUE mesg;
    ID exception;
    int n;

    mesg = Qnil;
    switch (argc) {
      case 0:
	mesg = Qnil;
	break;
      case 1:
	if (NIL_P(argv[0])) break;
	if (TYPE(argv[0]) == T_STRING) {
	    mesg = rb_exc_new3(rb_eRuntimeError, argv[0]);
	    break;
	}
	n = 0;
	goto exception_call;

      case 2:
      case 3:
	n = 1;
      exception_call:
	exception = rb_intern("exception");
	if (!rb_respond_to(argv[0], exception)) {
	    rb_raise(rb_eTypeError, "exception class/object expected");
	}
	mesg = rb_funcall(argv[0], exception, n, argv[1]);
	break;
      default:
	rb_raise(rb_eArgError, "wrong number of arguments");
	break;
    }
    if (argc > 0) {
	if (!rb_obj_is_kind_of(mesg, rb_eException))
	    rb_raise(rb_eTypeError, "exception object expected");
	set_backtrace(mesg, (argc>2)?argv[2]:Qnil);
    }

    if (ruby_frame != top_frame) {
	PUSH_FRAME();		/* fake frame */
	*ruby_frame = *_frame.prev->prev;
	rb_longjmp(TAG_RAISE, mesg);
	POP_FRAME();
    }
    rb_longjmp(TAG_RAISE, mesg);

    return Qnil;		/* not reached */
}

void
rb_jump_tag(tag)
    int tag;
{
    JUMP_TAG(tag);
}

int
rb_block_given_p()
{
    if (ruby_frame->iter && ruby_block)
	return Qtrue;
    return Qfalse;
}

int
rb_iterator_p()
{
    return rb_block_given_p();
}

static VALUE
rb_f_block_given_p()
{
    if (ruby_frame->prev && ruby_frame->prev->iter && ruby_block)
	return Qtrue;
    return Qfalse;
}

static VALUE
rb_yield_0(val, self, klass, flags, avalue)
    VALUE val, self, klass;	/* OK */
    int flags, avalue;
{
    NODE *node;
    volatile VALUE result = Qnil;
    volatile VALUE old_cref;
    volatile VALUE old_wrapper;
    struct BLOCK * volatile block;
    struct SCOPE * volatile old_scope;
    int old_vmode;
    struct FRAME frame;
    NODE *cnode = ruby_current_node;
    int state;
    static unsigned serial = 1;

    if (!rb_block_given_p()) {
	localjump_error("no block given", Qnil, 0);
    }

    PUSH_VARS();
    PUSH_CLASS();
    block = ruby_block;
    frame = block->frame;
    frame.prev = ruby_frame;
    ruby_frame = &(frame);
    old_cref = (VALUE)ruby_cref;
    ruby_cref = (NODE*)ruby_frame->cbase;
    old_wrapper = ruby_wrapper;
    ruby_wrapper = block->wrapper;
    old_scope = ruby_scope;
    ruby_scope = block->scope;
    old_vmode = scope_vmode;
    scope_vmode = (flags & YIELD_PUBLIC_DEF) ? SCOPE_PUBLIC : block->vmode;
    ruby_block = block->prev;
    if (block->flags & BLOCK_D_SCOPE) {
	/* put place holder for dynamic (in-block) local variables */
	ruby_dyna_vars = new_dvar(0, 0, block->dyna_vars);
    }
    else {
	/* FOR does not introduce new scope */
	ruby_dyna_vars = block->dyna_vars;
    }
    ruby_class = klass ? klass : block->klass;
    if (!klass) self = block->self;
    node = block->body;

    if (block->var) {
	PUSH_TAG(PROT_NONE);
	if ((state = EXEC_TAG()) == 0) {
	    if (block->var == (NODE*)1) { /* no parameter || */
		if ((flags & YIELD_PROC_CALL) && RARRAY(val)->len != 0) {
		    rb_raise(rb_eArgError, "wrong number of arguments (%ld for 0)",
			     RARRAY(val)->len);
		}
	    }
	    else if (block->var == (NODE*)2) {
		if (TYPE(val) == T_ARRAY && RARRAY(val)->len != 0) {
		    rb_raise(rb_eArgError, "wrong number of arguments (%ld for 0)",
			     RARRAY(val)->len);
		}
	    }
	    else if (nd_type(block->var) == NODE_MASGN) {
		if (!avalue) {
		    val = svalue_to_mrhs(val, block->var->nd_head);
		}
		massign(self, block->var, val, flags&YIELD_PROC_CALL);
	    }
	    else {
		int len = 0;
		if (avalue) {
		    len = RARRAY(val)->len;
		    if (len == 0) {
			goto zero_arg;
		    }
		    if (len == 1) {
			val = RARRAY(val)->ptr[0];
		    }
		    else {
			goto multi_values;
		    }
		}
		else if (val == Qundef) {
		  zero_arg:
		    val = Qnil;
		  multi_values:
		    {
			NODE *curr = ruby_current_node;
			ruby_current_node = block->var;
			rb_warn("multiple values for a block parameter (%d for 1)\n\tfrom %s:%d",
				len, curr->nd_file, nd_line(curr));
			ruby_current_node = curr;
		    }
		}
		assign(self, block->var, val, flags&YIELD_PROC_CALL);
	    }
	}
	POP_TAG();
	if (state) goto pop_state;
    }

    PUSH_ITER(block->iter);
    PUSH_TAG(PROT_NONE);
    if ((state = EXEC_TAG()) == 0) {
      redo:
	if (!node) {
	    result = Qnil;
	}
	else if (nd_type(node) == NODE_CFUNC || nd_type(node) == NODE_IFUNC) {
	    if (node->nd_state == YIELD_FUNC_AVALUE) {
		if (!avalue) {
		    val = svalue_to_avalue(val);
		}
	    }
	    else {
		if (avalue) {
		    val = avalue_to_svalue(val);
		}
		if (val == Qundef && node->nd_state != YIELD_FUNC_SVALUE)
		    val = Qnil;
	    }
	    result = (*node->nd_cfnc)(val, node->nd_tval, self);
	}
	else {
	    result = rb_eval(self, node);
	}
    }
    else {
	switch (state) {
	  case TAG_REDO:
	    state = 0;
	    CHECK_INTS;
	    goto redo;
	  case TAG_NEXT:
	    state = 0;
	    result = prot_tag->retval;
	    break;
	  case TAG_BREAK:
	  case TAG_RETURN:
	    state |= (serial++ << 8);
	    state |= 0x10;
	    block->tag->dst = state;
	    break;
	  default:
	    break;
	}
    }
    POP_TAG();
    POP_ITER();
  pop_state:
    POP_CLASS();
    if (ruby_dyna_vars && (block->flags & BLOCK_D_SCOPE) &&
	!FL_TEST(ruby_dyna_vars, DVAR_DONT_RECYCLE)) {
	struct RVarmap *vars = ruby_dyna_vars;

	if (ruby_dyna_vars->id == 0) {
	    vars = ruby_dyna_vars->next;
	    rb_gc_force_recycle((VALUE)ruby_dyna_vars);
	    while (vars && vars->id != 0 && vars != block->dyna_vars) {
		struct RVarmap *tmp = vars->next;
		rb_gc_force_recycle((VALUE)vars);
		vars = tmp;
	    }
	}
    }
    POP_VARS();
    ruby_block = block;
    ruby_frame = ruby_frame->prev;
    ruby_cref = (NODE*)old_cref;
    ruby_wrapper = old_wrapper;
    if (ruby_scope->flags & SCOPE_DONT_RECYCLE)
       scope_dup(old_scope);
    ruby_scope = old_scope;
    scope_vmode = old_vmode;
    ruby_current_node = cnode;
    if (state) {
	if (!block->tag) {
	    switch (state & TAG_MASK) {
	      case TAG_BREAK:
	      case TAG_RETURN:
		jump_tag_but_local_jump(state & TAG_MASK);
		break;
	    }
	}
	JUMP_TAG(state);
    }
    return result;
}

VALUE
rb_yield(val)
    VALUE val;
{
    return rb_yield_0(val, 0, 0, Qfalse, Qfalse);
}

VALUE
#ifdef HAVE_STDARG_PROTOTYPES
rb_yield_values(int n, ...)
#else
rb_yield_values(int n, va_alist)
    int n;
    va_dcl
#endif
{
    va_list args;
    VALUE ary;

    if (n == 0) {
	return rb_yield_0(Qundef, 0, 0, Qfalse, Qfalse);
    }
    ary = rb_ary_new2(n);
    va_init_list(args, n);
    while (n--) {
	rb_ary_push(ary, va_arg(args, VALUE));
    }
    va_end(args);
    return rb_yield_0(ary, 0, 0, Qfalse, Qtrue);
}

static VALUE
rb_f_loop()
{
    for (;;) {
	rb_yield_0(Qundef, 0, 0, Qfalse, Qfalse);
	CHECK_INTS;
    }
    return Qnil;		/* dummy */
}

static VALUE
massign(self, node, val, pcall)
    VALUE self;
    NODE *node;
    VALUE val;
    int pcall;
{
    NODE *list;
    long i = 0, len;

    len = RARRAY(val)->len;
    list = node->nd_head;
    for (; list && i<len; i++) {
	assign(self, list->nd_head, RARRAY(val)->ptr[i], pcall);
	list = list->nd_next;
    }
    if (pcall && list) goto arg_error;
    if (node->nd_args) {
	if (node->nd_args == (NODE*)-1) {
	    /* no check for mere `*' */
	}
	else if (!list && i<len) {
	    assign(self, node->nd_args, rb_ary_new4(len-i, RARRAY(val)->ptr+i), pcall);
	}
	else {
	    assign(self, node->nd_args, rb_ary_new2(0), pcall);
	}
    }
    else if (pcall && i < len) {
	goto arg_error;
    }

    while (list) {
	i++;
	assign(self, list->nd_head, Qnil, pcall);
	list = list->nd_next;
    }
    return val;

  arg_error:
    while (list) {
	i++;
	list = list->nd_next;
    }
    rb_raise(rb_eArgError, "wrong number of arguments (%ld for %ld)", len, i);
}

static void
assign(self, lhs, val, pcall)
    VALUE self;
    NODE *lhs;
    VALUE val;
    int pcall;
{
    ruby_current_node = lhs;
    if (val == Qundef) {
	rb_warning("assigning void value");
	val = Qnil;
    }
    switch (nd_type(lhs)) {
      case NODE_GASGN:
	rb_gvar_set(lhs->nd_entry, val);
	break;

      case NODE_IASGN:
	rb_ivar_set(self, lhs->nd_vid, val);
	break;

      case NODE_LASGN:
	if (ruby_scope->local_vars == 0)
	    rb_bug("unexpected local variable assignment");
	ruby_scope->local_vars[lhs->nd_cnt] = val;
	break;

      case NODE_DASGN:
	dvar_asgn(lhs->nd_vid, val);
	break;

      case NODE_DASGN_CURR:
	dvar_asgn_curr(lhs->nd_vid, val);
	break;

      case NODE_CDECL:
	if (lhs->nd_vid == 0) {
	    rb_const_set(class_prefix(self, lhs->nd_else), lhs->nd_else->nd_mid, val);
	}
	else {
	    rb_const_set(ruby_cbase, lhs->nd_vid, val);
	}
	break;

      case NODE_CVDECL:
	if (RTEST(ruby_verbose) && FL_TEST(ruby_cbase, FL_SINGLETON)) {
	    rb_warn("declaring singleton class variable");
	}
	rb_cvar_set(cvar_cbase(), lhs->nd_vid, val, Qtrue);
	break;

      case NODE_CVASGN:
	rb_cvar_set(cvar_cbase(), lhs->nd_vid, val, Qfalse);
	break;

      case NODE_MASGN:
	massign(self, lhs, svalue_to_mrhs(val, lhs->nd_head), pcall);
	break;

      case NODE_CALL:
      case NODE_ATTRASGN:
	{
	    VALUE recv;
	    int scope;
	    if (lhs->nd_recv == (NODE *)1) {
		recv = self;
		scope = 1;
	    }
	    else {
		recv = rb_eval(self, lhs->nd_recv);
		scope = 0;
	    }
	    if (!lhs->nd_args) {
		/* attr set */
		ruby_current_node = lhs;
		SET_CURRENT_SOURCE();
		rb_call(CLASS_OF(recv), recv, lhs->nd_mid, 1, &val, scope);
	    }
	    else {
		/* array set */
		VALUE args;

		args = rb_eval(self, lhs->nd_args);
		rb_ary_push(args, val);
		ruby_current_node = lhs;
		SET_CURRENT_SOURCE();
		rb_call(CLASS_OF(recv), recv, lhs->nd_mid,
			RARRAY(args)->len, RARRAY(args)->ptr, scope);
	    }
	}
	break;

      default:
	rb_bug("bug in variable assignment");
	break;
    }
}

VALUE
rb_iterate(it_proc, data1, bl_proc, data2)
    VALUE (*it_proc) _((VALUE)), (*bl_proc)(ANYARGS);
    VALUE data1, data2;
{
    int state;
    volatile VALUE retval = Qnil;
    NODE *node = NEW_IFUNC(bl_proc, data2);
    VALUE self = ruby_top_self;

  iter_retry:
    PUSH_ITER(ITER_PRE);
    PUSH_BLOCK(0, node);
    PUSH_TAG(PROT_NONE);

    state = EXEC_TAG();
    if (state == 0) {
	retval = (*it_proc)(data1);
    }
    if (ruby_block->tag->dst == state) {
	state &= TAG_MASK;
	if (state == TAG_RETURN || state == TAG_BREAK) {
	    retval = prot_tag->retval;
	}
    }
    POP_TAG();
    POP_BLOCK();
    POP_ITER();

    switch (state) {
      case 0:
	break;

      case TAG_RETRY:
	goto iter_retry;

      case TAG_BREAK:
	break;

      case TAG_RETURN:
	return_value(retval);
	/* fall through */
      default:
	JUMP_TAG(state);
    }
    return retval;
}

static int
handle_rescue(self, node)
    VALUE self;
    NODE *node;
{
    int argc; VALUE *argv; /* used in SETUP_ARGS */
    TMP_PROTECT;

    if (!node->nd_args) {
	return rb_obj_is_kind_of(ruby_errinfo, rb_eStandardError);
    }

    BEGIN_CALLARGS;
    SETUP_ARGS(node->nd_args);
    END_CALLARGS;

    while (argc--) {
	if (!rb_obj_is_kind_of(argv[0], rb_cModule)) {
	    rb_raise(rb_eTypeError, "class or module required for rescue clause");
	}
	if (RTEST(rb_funcall(*argv, eqq, 1, ruby_errinfo))) return 1;
	argv++;
    }
    return 0;
}

VALUE
#ifdef HAVE_STDARG_PROTOTYPES
rb_rescue2(VALUE (*b_proc)(ANYARGS), VALUE data1, VALUE (*r_proc)(ANYARGS), VALUE data2, ...)
#else
rb_rescue2(b_proc, data1, r_proc, data2, va_alist)
    VALUE (*b_proc)(ANYARGS), (*r_proc)(ANYARGS);
    VALUE data1, data2;
    va_dcl
#endif
{
    int state;
    volatile VALUE result;
    volatile VALUE e_info = ruby_errinfo;
    va_list args;

    PUSH_TAG(PROT_NONE);
    if ((state = EXEC_TAG()) == 0) {
      retry_entry:
	result = (*b_proc)(data1);
    }
    else if (state == TAG_RAISE) {
	int handle = Qfalse;
	VALUE eclass;

	va_init_list(args, data2);
	while (eclass = va_arg(args, VALUE)) {
	    if (rb_obj_is_kind_of(ruby_errinfo, eclass)) {
		handle = Qtrue;
		break;
	    }
	}
	va_end(args);

	if (handle) {
	    if (r_proc) {
		PUSH_TAG(PROT_NONE);
		if ((state = EXEC_TAG()) == 0) {
		    result = (*r_proc)(data2, ruby_errinfo);
		}
		POP_TAG();
		if (state == TAG_RETRY) {
		    state = 0;
		    ruby_errinfo = Qnil;
		    goto retry_entry;
		}
	    }
	    else {
		result = Qnil;
		state = 0;
	    }
	    if (state == 0) {
		ruby_errinfo = e_info;
	    }
	}
    }
    POP_TAG();
    if (state) JUMP_TAG(state);

    return result;
}

VALUE
rb_rescue(b_proc, data1, r_proc, data2)
    VALUE (*b_proc)(), (*r_proc)();
    VALUE data1, data2;
{
    return rb_rescue2(b_proc, data1, r_proc, data2, rb_eStandardError, (VALUE)0);
}

VALUE
rb_protect(proc, data, state)
    VALUE (*proc) _((VALUE));
    VALUE data;
    int *state;
{
    VALUE result;		/* OK */
    int status;

    PUSH_TAG(PROT_NONE);
    if ((status = EXEC_TAG()) == 0) {
	result = (*proc)(data);
    }
    POP_TAG();
    if (state) {
	*state = status;
    }
    if (status != 0) {
	return Qnil;
    }

    return result;
}

VALUE
rb_ensure(b_proc, data1, e_proc, data2)
    VALUE (*b_proc)();
    VALUE data1;
    VALUE (*e_proc)();
    VALUE data2;
{
    int state;
    volatile VALUE result = Qnil;
    VALUE retval;

    PUSH_TAG(PROT_NONE);
    if ((state = EXEC_TAG()) == 0) {
	result = (*b_proc)(data1);
    }
    POP_TAG();
    retval = prot_tag ? prot_tag->retval : Qnil;	/* save retval */
    (*e_proc)(data2);
    if (prot_tag) return_value(retval);

    if (state) JUMP_TAG(state);
    return result;
}

VALUE
rb_with_disable_interrupt(proc, data)
    VALUE (*proc)();
    VALUE data;
{
    VALUE result;		/* OK */
    int status;

    DEFER_INTS;
    PUSH_TAG(PROT_NONE);
    if ((status = EXEC_TAG()) == 0) {
	result = (*proc)(data);
    }
    POP_TAG();
    ALLOW_INTS;
    if (status) JUMP_TAG(status);

    return result;
}

static inline void
stack_check()
{
    static int overflowing = 0;

    if (!overflowing && ruby_stack_check()) {
	int state;
	overflowing = 1;
	PUSH_TAG(PROT_NONE);
	if ((state = EXEC_TAG()) == 0) {
	    rb_exc_raise(sysstack_error);
	}
	POP_TAG();
	overflowing = 0;
	JUMP_TAG(state);
    }
}

static int last_call_status;

#define CSTAT_PRIV  1
#define CSTAT_PROT  2
#define CSTAT_VCALL 4
#define CSTAT_SUPER 8

static VALUE
rb_method_missing(argc, argv, obj)
    int argc;
    VALUE *argv;
    VALUE obj;
{
    ID id;
    VALUE exc = rb_eNoMethodError;
    volatile VALUE d = 0;
    char *format = 0;
    char *desc = "";
    NODE *cnode = ruby_current_node;

    if (argc == 0 || !SYMBOL_P(argv[0])) {
	rb_raise(rb_eArgError, "no id given");
    }

    stack_check();

    id = SYM2ID(argv[0]);

    switch (TYPE(obj)) {
      case T_NIL:
	desc = "nil";
	break;
      case T_TRUE:
	desc = "true";
	break;
      case T_FALSE:
	desc = "false";
	break;
      default:
	if (rb_respond_to(obj, rb_intern("inspect")))
	    d = rb_inspect(obj);
	else
	    d = rb_any_to_s(obj);
	break;
    }
    if (d) {
	if (RSTRING(d)->len > 65) {
	    d = rb_any_to_s(obj);
	}
	desc = RSTRING(d)->ptr;
    }

    if (last_call_status & CSTAT_PRIV) {
	format = "private method `%s' called for %s%s%s";
    }
    else if (last_call_status & CSTAT_PROT) {
	format = "protected method `%s' called for %s%s%s";
    }
    else if (last_call_status & CSTAT_VCALL) {
	format = "undefined local variable or method `%s' for %s%s%s";
	exc = rb_eNameError;
    }
    else if (last_call_status & CSTAT_SUPER) {
	format = "super: no superclass method `%s'";
    }
    if (!format) {
	format = "undefined method `%s' for %s%s%s";
    }

    ruby_current_node = cnode;
    {
	char buf[BUFSIZ];
	int noclass = (!desc || desc[0]=='#');
	int n = 0;
	VALUE args[3];

	snprintf(buf, BUFSIZ, format, rb_id2name(id),
		 desc, noclass ? "" : ":",
		 noclass ? "" : rb_obj_classname(obj));
	args[n++] = rb_str_new2(buf);
	args[n++] = argv[0];
	if (exc == rb_eNoMethodError) {
	    args[n++] = rb_ary_new4(argc-1, argv+1);
	}
	exc = rb_class_new_instance(n, args, exc);
	ruby_frame = ruby_frame->prev; /* pop frame for "method_missing" */
	rb_exc_raise(exc);
    }

    return Qnil;		/* not reached */
}

static VALUE
method_missing(obj, id, argc, argv, call_status)
    VALUE obj;
    ID    id;
    int   argc;
    const VALUE *argv;
    int   call_status;
{
    VALUE *nargv;

    last_call_status = call_status;

    if (id == missing) {
	PUSH_FRAME();
	rb_method_missing(argc, argv, obj);
	POP_FRAME();
    }
    else if (id == ID_ALLOCATOR) {
	rb_raise(rb_eNoMethodError, "allocator undefined for %s", rb_class2name(obj));
    }

    nargv = ALLOCA_N(VALUE, argc+1);
    nargv[0] = ID2SYM(id);
    MEMCPY(nargv+1, argv, VALUE, argc);

    return rb_funcall2(obj, missing, argc+1, nargv);
}

static inline VALUE
call_cfunc(func, recv, len, argc, argv)
    VALUE (*func)();
    VALUE recv;
    int len, argc;
    VALUE *argv;
{
    if (len >= 0 && argc != len) {
	rb_raise(rb_eArgError, "wrong number of arguments(%d for %d)",
		 argc, len);
    }

    switch (len) {
      case -2:
	return (*func)(recv, rb_ary_new4(argc, argv));
	break;
      case -1:
	return (*func)(argc, argv, recv);
	break;
      case 0:
	return (*func)(recv);
	break;
      case 1:
	return (*func)(recv, argv[0]);
	break;
      case 2:
	return (*func)(recv, argv[0], argv[1]);
	break;
      case 3:
	return (*func)(recv, argv[0], argv[1], argv[2]);
	break;
      case 4:
	return (*func)(recv, argv[0], argv[1], argv[2], argv[3]);
	break;
      case 5:
	return (*func)(recv, argv[0], argv[1], argv[2], argv[3], argv[4]);
	break;
      case 6:
	return (*func)(recv, argv[0], argv[1], argv[2], argv[3], argv[4],
		       argv[5]);
	break;
      case 7:
	return (*func)(recv, argv[0], argv[1], argv[2], argv[3], argv[4],
		       argv[5], argv[6]);
	break;
      case 8:
	return (*func)(recv, argv[0], argv[1], argv[2], argv[3], argv[4],
		       argv[5], argv[6], argv[7]);
	break;
      case 9:
	return (*func)(recv, argv[0], argv[1], argv[2], argv[3], argv[4],
		       argv[5], argv[6], argv[7], argv[8]);
	break;
      case 10:
	return (*func)(recv, argv[0], argv[1], argv[2], argv[3], argv[4],
		       argv[5], argv[6], argv[7], argv[8], argv[9]);
	break;
      case 11:
	return (*func)(recv, argv[0], argv[1], argv[2], argv[3], argv[4],
		       argv[5], argv[6], argv[7], argv[8], argv[9], argv[10]);
	break;
      case 12:
	return (*func)(recv, argv[0], argv[1], argv[2], argv[3], argv[4],
		       argv[5], argv[6], argv[7], argv[8], argv[9],
		       argv[10], argv[11]);
	break;
      case 13:
	return (*func)(recv, argv[0], argv[1], argv[2], argv[3], argv[4],
		       argv[5], argv[6], argv[7], argv[8], argv[9], argv[10],
		       argv[11], argv[12]);
	break;
      case 14:
	return (*func)(recv, argv[0], argv[1], argv[2], argv[3], argv[4],
		       argv[5], argv[6], argv[7], argv[8], argv[9], argv[10],
		       argv[11], argv[12], argv[13]);
	break;
      case 15:
	return (*func)(recv, argv[0], argv[1], argv[2], argv[3], argv[4],
		       argv[5], argv[6], argv[7], argv[8], argv[9], argv[10],
		       argv[11], argv[12], argv[13], argv[14]);
	break;
      default:
	rb_raise(rb_eArgError, "too many arguments(%d)", len);
	break;
    }
    return Qnil;		/* not reached */
}

static VALUE
rb_call0(klass, recv, id, oid, argc, argv, body, nosuper)
    VALUE klass, recv;
    ID    id;
    ID    oid;
    int argc;			/* OK */
    VALUE *argv;		/* OK */
    NODE *body;			/* OK */
    int nosuper;
{
    NODE *b2;		/* OK */
    volatile VALUE result = Qnil;
    int itr;
    static int tick;
    TMP_PROTECT;

    switch (ruby_iter->iter) {
      case ITER_PRE:
	itr = ITER_CUR;
	break;
      case ITER_CUR:
      default:
	itr = ITER_NOT;
	break;
    }

    if ((++tick & 0xff) == 0) {
	CHECK_INTS;		/* better than nothing */
	stack_check();
    }
    PUSH_ITER(itr);
    PUSH_FRAME();

    ruby_frame->last_func = id;
    ruby_frame->orig_func = oid;
    ruby_frame->last_class = nosuper?0:klass;
    ruby_frame->self = recv;
    ruby_frame->argc = argc;
    ruby_frame->argv = argv;

    switch (nd_type(body)) {
      case NODE_CFUNC:
	{
	    int len = body->nd_argc;

	    if (len < -2) {
		rb_bug("bad argc(%d) specified for `%s(%s)'",
		       len, rb_class2name(klass), rb_id2name(id));
	    }
	    if (trace_func) {
		int state;

		call_trace_func("c-call", ruby_current_node, recv, id, klass);
		PUSH_TAG(PROT_FUNC);
		if ((state = EXEC_TAG()) == 0) {
		    result = call_cfunc(body->nd_cfnc, recv, len, argc, argv);
		}
		POP_TAG();
		ruby_current_node = ruby_frame->node;
		call_trace_func("c-return", ruby_current_node, recv, id, klass);
		if (state) JUMP_TAG(state);
	    }
	    else {
		result = call_cfunc(body->nd_cfnc, recv, len, argc, argv);
	    }
	}
	break;

	/* for attr get/set */
      case NODE_IVAR:
	if (argc != 0) {
	    rb_raise(rb_eArgError, "wrong number of arguments(%d for 0)", argc);
	}
	result = rb_attr_get(recv, body->nd_vid);
	break;

      case NODE_ATTRSET:
	/* for re-scoped/renamed method */
      case NODE_ZSUPER:
	result = rb_eval(recv, body);
	break;

      case NODE_DMETHOD:
	result = method_call(argc, argv, umethod_bind(body->nd_cval, recv));
	break;

      case NODE_BMETHOD:
	result = proc_invoke(body->nd_cval, rb_ary_new4(argc, argv), recv, klass);
	break;

      case NODE_SCOPE:
	{
	    int state;
	    VALUE *local_vars;	/* OK */
	    NODE *saved_cref = 0;

	    PUSH_SCOPE();

	    if (body->nd_rval) {
		saved_cref = ruby_cref;
		ruby_cref = (NODE*)body->nd_rval;
		ruby_frame->cbase = body->nd_rval;
	    }
	    if (body->nd_tbl) {
		local_vars = TMP_ALLOC(body->nd_tbl[0]+1);
		*local_vars++ = (VALUE)body;
		rb_mem_clear(local_vars, body->nd_tbl[0]);
		ruby_scope->local_tbl = body->nd_tbl;
		ruby_scope->local_vars = local_vars;
	    }
	    else {
		local_vars = ruby_scope->local_vars = 0;
		ruby_scope->local_tbl  = 0;
	    }
	    b2 = body = body->nd_next;

	    PUSH_VARS();
	    PUSH_TAG(PROT_FUNC);

	    if ((state = EXEC_TAG()) == 0) {
		NODE *node = 0;
		int i;

		if (nd_type(body) == NODE_ARGS) {
		    node = body;
		    body = 0;
		}
		else if (nd_type(body) == NODE_BLOCK) {
		    node = body->nd_head;
		    body = body->nd_next;
		}
		if (node) {
		    if (nd_type(node) != NODE_ARGS) {
			rb_bug("no argument-node");
		    }

		    i = node->nd_cnt;
		    if (i > argc) {
			rb_raise(rb_eArgError, "wrong number of arguments(%d for %d)",
				 argc, i);
		    }
		    if (node->nd_rest == -1) {
			int opt = i;
			NODE *optnode = node->nd_opt;

			while (optnode) {
			    opt++;
			    optnode = optnode->nd_next;
			}
			if (opt < argc) {
			    rb_raise(rb_eArgError, "wrong number of arguments(%d for %d)",
				     argc, opt);
			}
			ruby_frame->argc = opt;
			ruby_frame->argv = local_vars+2;
		    }

		    if (local_vars) {
			if (i > 0) {
			    /* +2 for $_ and $~ */
			    MEMCPY(local_vars+2, argv, VALUE, i);
			}
			argv += i; argc -= i;
			if (node->nd_opt) {
			    NODE *opt = node->nd_opt;

			    while (opt && argc) {
				assign(recv, opt->nd_head, *argv, 1);
				argv++; argc--;
				opt = opt->nd_next;
			    }
			    if (opt) {
				rb_eval(recv, opt);
			    }
			}
			local_vars = ruby_scope->local_vars;
			if (node->nd_rest >= 0) {
			    VALUE v;

			    if (argc > 0)
				v = rb_ary_new4(argc,argv);
			    else
				v = rb_ary_new2(0);
			    ruby_scope->local_vars[node->nd_rest] = v;
			}
		    }
		}

		if (trace_func) {
		    call_trace_func("call", b2, recv, id, klass);
		}
		result = rb_eval(recv, body);
	    }
	    else if (state == TAG_RETURN) {
		result = prot_tag->retval;
		state = 0;
	    }
	    POP_TAG();
	    POP_VARS();
	    POP_SCOPE();
	    ruby_cref = saved_cref;
	    if (trace_func) {
		call_trace_func("return", ruby_frame->prev->node, recv, id, klass);
	    }
	    switch (state) {
	      case 0:
		break;

	      case TAG_RETRY:
		if (rb_block_given_p()) {
                   JUMP_TAG(state);
		}
		/* fall through */
	      default:
		jump_tag_but_local_jump(state);
		break;
	    }
	}
	break;

      default:
	rb_bug("unknown node type %d", nd_type(body));
	break;
    }
    POP_FRAME();
    POP_ITER();
    return result;
}

static VALUE
rb_call(klass, recv, mid, argc, argv, scope)
    VALUE klass, recv;
    ID    mid;
    int argc;			/* OK */
    const VALUE *argv;		/* OK */
    int scope;
{
    NODE  *body;		/* OK */
    int    noex;
    ID     id = mid;
    struct cache_entry *ent;

    if (!klass) {
	rb_raise(rb_eNotImpError, "method `%s' called on terminated object (0x%lx)",
		 rb_id2name(mid), recv);
    }
    /* is it in the method cache? */
    ent = cache + EXPR1(klass, mid);
    if (ent->mid == mid && ent->klass == klass) {
	if (!ent->method)
	    return method_missing(recv, mid, argc, argv, scope==2?CSTAT_VCALL:0);
	klass = ent->origin;
	id    = ent->mid0;
	noex  = ent->noex;
	body  = ent->method;
    }
    else if ((body = rb_get_method_body(&klass, &id, &noex)) == 0) {
	if (scope == 3) {
	    return method_missing(recv, mid, argc, argv, CSTAT_SUPER);
	}
	return method_missing(recv, mid, argc, argv, scope==2?CSTAT_VCALL:0);
    }

    if (mid != missing) {
	/* receiver specified form for private method */
	if ((noex & NOEX_PRIVATE) && scope == 0)
	    return method_missing(recv, mid, argc, argv, CSTAT_PRIV);

	/* self must be kind of a specified form for protected method */
	if ((noex & NOEX_PROTECTED)) {
	    VALUE defined_class = klass;

	    if (TYPE(defined_class) == T_ICLASS) {
		defined_class = RBASIC(defined_class)->klass;
	    }
	    if (!rb_obj_is_kind_of(ruby_frame->self, rb_class_real(defined_class)))
		return method_missing(recv, mid, argc, argv, CSTAT_PROT);
	}
    }

    return rb_call0(klass, recv, mid, id, argc, argv, body, noex & NOEX_NOSUPER);
}

VALUE
rb_apply(recv, mid, args)
    VALUE recv;
    ID mid;
    VALUE args;
{
    int argc;
    VALUE *argv;

    argc = RARRAY(args)->len; /* Assigns LONG, but argc is INT */
    argv = ALLOCA_N(VALUE, argc);
    MEMCPY(argv, RARRAY(args)->ptr, VALUE, argc);
    return rb_call(CLASS_OF(recv), recv, mid, argc, argv, 1);
}

static VALUE
rb_f_send(argc, argv, recv)
    int argc;
    VALUE *argv;
    VALUE recv;
{
    VALUE vid;

    if (argc == 0) rb_raise(rb_eArgError, "no method name given");

    vid = *argv++; argc--;
    PUSH_ITER(rb_block_given_p()?ITER_PRE:ITER_NOT);
    vid = rb_call(CLASS_OF(recv), recv, rb_to_id(vid), argc, argv, 1);
    POP_ITER();

    return vid;
}

VALUE
#ifdef HAVE_STDARG_PROTOTYPES
rb_funcall(VALUE recv, ID mid, int n, ...)
#else
rb_funcall(recv, mid, n, va_alist)
    VALUE recv;
    ID mid;
    int n;
    va_dcl
#endif
{
    va_list ar;
    VALUE *argv;

    if (n > 0) {
	long i;

	argv = ALLOCA_N(VALUE, n);

	va_init_list(ar, n);
	for (i=0;i<n;i++) {
	    argv[i] = va_arg(ar, VALUE);
	}
	va_end(ar);
    }
    else {
	argv = 0;
    }

    return rb_call(CLASS_OF(recv), recv, mid, n, argv, 1);
}

VALUE
rb_funcall2(recv, mid, argc, argv)
    VALUE recv;
    ID mid;
    int argc;
    const VALUE *argv;
{
    return rb_call(CLASS_OF(recv), recv, mid, argc, argv, 1);
}

VALUE
rb_funcall3(recv, mid, argc, argv)
    VALUE recv;
    ID mid;
    int argc;
    const VALUE *argv;
{
    return rb_call(CLASS_OF(recv), recv, mid, argc, argv, 0);
}

VALUE
rb_call_super(argc, argv)
    int argc;
    const VALUE *argv;
{
    VALUE result, self, klass, k;

    if (ruby_frame->last_class == 0) {	
	rb_name_error(ruby_frame->last_func, "calling `super' from `%s' is prohibited",
		      rb_id2name(ruby_frame->last_func));
    }

    self = ruby_frame->self;
    klass = ruby_frame->last_class;
    if (BUILTIN_TYPE(klass) == T_MODULE) {
	k = CLASS_OF(self);
	while (!(BUILTIN_TYPE(k) == T_ICLASS && RBASIC(k)->klass == klass)) {
	    k = RCLASS(k)->super;
	    if (!k) {
		rb_raise(rb_eTypeError, "%s is not included in %s",
			 rb_class2name(klass),
			 rb_class2name(CLASS_OF(self)));
	    }
	}
	if (RCLASS(k)->super == 0) {
	    rb_name_error(ruby_frame->last_func,
			  "super: no superclass method `%s'",
			  rb_id2name(ruby_frame->last_func));
	}
	klass = k;
    }

    PUSH_ITER(ruby_iter->iter || rb_block_given_p() ? ITER_PRE : ITER_NOT);
    result = rb_call(RCLASS(klass)->super, self, ruby_frame->orig_func, argc, argv, 3);
    POP_ITER();

    return result;
}

static VALUE
backtrace(lev)
    int lev;
{
    struct FRAME *frame = ruby_frame;
    char buf[BUFSIZ];
    VALUE ary;
    NODE *n;

    ary = rb_ary_new();
    if (frame->last_func == ID_ALLOCATOR) {
	frame = frame->prev;
    }
    if (lev < 0) {
	ruby_set_current_source();
	if (frame->last_func) {
	    snprintf(buf, BUFSIZ, "%s:%d:in `%s'",
		     ruby_sourcefile, ruby_sourceline,
		     rb_id2name(frame->last_func));
	}
	else if (ruby_sourceline == 0) {
	    snprintf(buf, BUFSIZ, "%s", ruby_sourcefile);
	}
	else {
	    snprintf(buf, BUFSIZ, "%s:%d", ruby_sourcefile, ruby_sourceline);
	}
	rb_ary_push(ary, rb_str_new2(buf));
    }
    else {
	while (lev-- > 0) {
	    frame = frame->prev;
	    if (!frame) {
		ary = Qnil;
		break;
	    }
	}
    }
    while (frame && (n = frame->node)) {
	if (frame->prev && frame->prev->last_func) {
	    snprintf(buf, BUFSIZ, "%s:%d:in `%s'",
		     n->nd_file, nd_line(n),
		     rb_id2name(frame->prev->last_func));
	}
	else {
	    snprintf(buf, BUFSIZ, "%s:%d", n->nd_file, nd_line(n));
	}
	rb_ary_push(ary, rb_str_new2(buf));
	frame = frame->prev;
    }

    return ary;
}

static VALUE
rb_f_caller(argc, argv)
    int argc;
    VALUE *argv;
{
    VALUE level;
    int lev;

    rb_scan_args(argc, argv, "01", &level);

    if (NIL_P(level)) lev = 1;
    else lev = NUM2INT(level);
    if (lev < 0) rb_raise(rb_eArgError, "negative level(%d)", lev);

    return backtrace(lev);
}

void
rb_backtrace()
{
    long i;
    VALUE ary;

    ary = backtrace(-1);
    for (i=0; i<RARRAY(ary)->len; i++) {
	printf("\tfrom %s\n", RSTRING(RARRAY(ary)->ptr[i])->ptr);
    }
}

static VALUE
make_backtrace()
{
    return backtrace(-1);
}

ID
rb_frame_last_func()
{
    return ruby_frame->last_func;
}

static NODE*
compile(src, file, line)
    VALUE src;
    char *file;
    int line;
{
    NODE *node;
    int critical;

    ruby_nerrs = 0;
    StringValue(src);
    critical = rb_thread_critical;
    rb_thread_critical = Qtrue;
    node = rb_compile_string(file, src, line);
    rb_thread_critical = critical;
    

    if (ruby_nerrs == 0) return node;
    return 0;
}

static VALUE
eval(self, src, scope, file, line)
    VALUE self, src, scope;
    char *file;
    int line;
{
    struct BLOCK *data = NULL;
    volatile VALUE result = Qnil;
    struct SCOPE * volatile old_scope;
    struct BLOCK * volatile old_block;
    struct RVarmap * volatile old_dyna_vars;
    VALUE volatile old_cref;
    int volatile old_vmode;
    volatile VALUE old_wrapper;
    struct FRAME frame;
    NODE *nodesave = ruby_current_node;
    volatile int iter = ruby_frame->iter;
    int state;

    if (!NIL_P(scope)) {
	if (!rb_obj_is_proc(scope)) {
	    rb_raise(rb_eTypeError, "wrong argument type %s (expected Proc/Binding)",
		     rb_obj_classname(scope));
	}

	Data_Get_Struct(scope, struct BLOCK, data);
	/* PUSH BLOCK from data */
	frame = data->frame;
	frame.tmp = ruby_frame;	/* gc protection */
	ruby_frame = &(frame);
	old_scope = ruby_scope;
	ruby_scope = data->scope;
	old_block = ruby_block;
	ruby_block = data->prev;
	old_dyna_vars = ruby_dyna_vars;
	ruby_dyna_vars = data->dyna_vars;
	old_vmode = scope_vmode;
	scope_vmode = data->vmode;
	old_cref = (VALUE)ruby_cref;
	ruby_cref = (NODE*)ruby_frame->cbase;
	old_wrapper = ruby_wrapper;
	ruby_wrapper = data->wrapper;
	if ((file == 0 || (line == 1 && strcmp(file, "(eval)") == 0)) && data->body) {
	    file = data->body->nd_file;
	    if (!file) file = "__builtin__";
	    line = nd_line(data->body);
	}

	self = data->self;
	ruby_frame->iter = data->iter;
    }
    else {
	if (ruby_frame->prev) {
	    ruby_frame->iter = ruby_frame->prev->iter;
	}
    }
    if (file == 0) {
	ruby_set_current_source();
	file = ruby_sourcefile;
	line = ruby_sourceline;
    }
    PUSH_CLASS();
    ruby_class = ruby_cbase;

    ruby_in_eval++;
    if (TYPE(ruby_class) == T_ICLASS) {
	ruby_class = RBASIC(ruby_class)->klass;
    }
    PUSH_TAG(PROT_NONE);
    if ((state = EXEC_TAG()) == 0) {
	NODE *node;

	result = ruby_errinfo;
	ruby_errinfo = Qnil;
	node = compile(src, file, line);
	if (ruby_nerrs > 0) {
	    compile_error(0);
	}
	if (!NIL_P(result)) ruby_errinfo = result;
	result = eval_node(self, node); 
    }
    POP_TAG();
    POP_CLASS();
    ruby_in_eval--;
    if (!NIL_P(scope)) {
	int dont_recycle = ruby_scope->flags & SCOPE_DONT_RECYCLE;

	ruby_wrapper = old_wrapper;
	ruby_cref  = (NODE*)old_cref;
	ruby_frame = frame.tmp;
	ruby_scope = old_scope;
	ruby_block = old_block;
	ruby_dyna_vars = old_dyna_vars;
	data->vmode = scope_vmode; /* write back visibility mode */
	scope_vmode = old_vmode;
	if (dont_recycle) {
	   struct tag *tag;
	   struct RVarmap *vars;

           scope_dup(ruby_scope);
	   for (tag=prot_tag; tag; tag=tag->prev) {
	       scope_dup(tag->scope);
	   }
	   if (ruby_block) {
	       struct BLOCK *block = ruby_block;
	       while (block) {
		   block->tag->flags |= BLOCK_DYNAMIC;
		   block = block->prev;
	       }
	   }
	   for (vars = ruby_dyna_vars; vars; vars = vars->next) {
	       FL_SET(vars, DVAR_DONT_RECYCLE);
	   }
	}
    }
    else {
	ruby_frame->iter = iter;
    }
    ruby_current_node = nodesave;
    ruby_set_current_source();
    if (state) {
	if (state == TAG_RAISE) {
	    VALUE err, errat, mesg;

	    mesg = rb_obj_as_string(ruby_errinfo);
	    if (strcmp(file, "(eval)") == 0) {
		if (ruby_sourceline > 1) {
		    errat = get_backtrace(ruby_errinfo);
		    err = rb_str_dup(RARRAY(errat)->ptr[0]);
		    rb_str_cat2(err, ": ");
		    rb_str_append(err, mesg);
		}
		else {
		    err = mesg;
		}
		rb_exc_raise(rb_funcall(ruby_errinfo, rb_intern("exception"), 1, err));
	    }
	    rb_exc_raise(ruby_errinfo);
	}
	JUMP_TAG(state);
    }

    return result;
}

static VALUE
rb_f_eval(argc, argv, self)
    int argc;
    VALUE *argv;
    VALUE self;
{
    VALUE src, scope, vfile, vline;
    char *file = "(eval)";
    int line = 1;

    rb_scan_args(argc, argv, "13", &src, &scope, &vfile, &vline);
    if (ruby_safe_level >= 4) {
	StringValue(src);
	if (!NIL_P(scope) && !OBJ_TAINTED(scope)) {
	    rb_raise(rb_eSecurityError, "Insecure: can't modify trusted binding");
	}
    }
    else {
	SafeStringValue(src);
    }
    if (argc >= 3) {
	file = StringValuePtr(vfile);
    }
    if (argc >= 4) {
	line = NUM2INT(vline);
    }

    if (NIL_P(scope) && ruby_frame->prev) {
	struct FRAME *prev;
	VALUE val;

	prev = ruby_frame;
	PUSH_FRAME();
	*ruby_frame = *prev->prev;
	ruby_frame->prev = prev;
	val = eval(self, src, scope, file, line);
	POP_FRAME();

	return val;
    }
    return eval(self, src, scope, file, line);
}

/* function to call func under the specified class/module context */
static VALUE
exec_under(func, under, cbase, args)
    VALUE (*func)();
    VALUE under, cbase;
    void *args;
{
    VALUE val;			/* OK */
    int state;
    int mode;

    PUSH_CLASS();
    ruby_class = under;
    PUSH_FRAME();
    ruby_frame->self = _frame.prev->self;
    ruby_frame->last_func = _frame.prev->last_func;
    ruby_frame->last_class = _frame.prev->last_class;
    ruby_frame->argc = _frame.prev->argc;
    ruby_frame->argv = _frame.prev->argv;
    if (cbase) {
	if (ruby_cbase != cbase) {
	    ruby_frame->cbase = (VALUE)rb_node_newnode(NODE_CREF,under,0,ruby_frame->cbase);
	}
	PUSH_CREF(cbase);
    }

    mode = scope_vmode;
    SCOPE_SET(SCOPE_PUBLIC);
    PUSH_TAG(PROT_NONE);
    if ((state = EXEC_TAG()) == 0) {
	val = (*func)(args);
    }
    POP_TAG();
    if (cbase) POP_CREF();
    SCOPE_SET(mode);
    POP_FRAME();
    POP_CLASS();
    if (state) JUMP_TAG(state);

    return val;
}

static VALUE
eval_under_i(args)
    VALUE *args;
{
    return eval(args[0], args[1], Qnil, (char*)args[2], (int)args[3]);
}

/* string eval under the class/module context */
static VALUE
eval_under(under, self, src, file, line)
    VALUE under, self, src;
    const char *file;
    int line;
{
    VALUE args[4];

    if (ruby_safe_level >= 4) {
	StringValue(src);
    }
    else {
	SafeStringValue(src);
    }
    args[0] = self;
    args[1] = src;
    args[2] = (VALUE)file;
    args[3] = (VALUE)line;
    return exec_under(eval_under_i, under, under, args);
}

static VALUE
yield_under_i(self)
    VALUE self;
{
    return rb_yield_0(self, self, ruby_class, YIELD_PUBLIC_DEF, Qfalse);
}

/* block eval under the class/module context */
static VALUE
yield_under(under, self)
    VALUE under, self;
{
    return exec_under(yield_under_i, under, 0, self);
}

static VALUE
specific_eval(argc, argv, klass, self)
    int argc;
    VALUE *argv;
    VALUE klass, self;
{
    if (rb_block_given_p()) {
	if (argc > 0) {
	    rb_raise(rb_eArgError, "wrong number of arguments (%d for 0)", argc);
	}
	return yield_under(klass, self);
    }
    else {
	char *file = "(eval)";
	int   line = 1;

	if (argc == 0) {
	    rb_raise(rb_eArgError, "block not supplied");
	}
	else {
	    if (ruby_safe_level >= 4) {
		StringValue(argv[0]);
	    }
	    else {
		SafeStringValue(argv[0]);
	    }
	    if (argc > 3) {
		rb_raise(rb_eArgError, "wrong number of arguments: %s(src) or %s{..}",
			 rb_id2name(ruby_frame->last_func),
			 rb_id2name(ruby_frame->last_func));
	    }
	    if (argc > 1) {
		file = StringValuePtr(argv[1]);
	    }
	    if (argc > 2) line = NUM2INT(argv[2]);
	}
	return eval_under(klass, self, argv[0], file, line);
    }
}

VALUE
rb_obj_instance_eval(argc, argv, self)
    int argc;
    VALUE *argv;
    VALUE self;
{
    VALUE klass;

    if (rb_special_const_p(self)) {
	klass = Qnil;
    }
    else {
	klass = rb_singleton_class(self);
    }

    return specific_eval(argc, argv, klass, self);
}

VALUE
rb_mod_module_eval(argc, argv, mod)
    int argc;
    VALUE *argv;
    VALUE mod;
{
    return specific_eval(argc, argv, mod, mod);
}

VALUE rb_load_path;

void
rb_load(fname, wrap)
    VALUE fname;
    int wrap;
{
    VALUE tmp;
    int state;
    volatile ID last_func;
    volatile VALUE wrapper = 0;
    volatile VALUE self = ruby_top_self;
    NODE *saved_cref = ruby_cref;
    TMP_PROTECT;

    if (wrap && ruby_safe_level >= 4) {
	StringValue(fname);
    }
    else {
	SafeStringValue(fname);
    }
    tmp = rb_find_file(fname);
    if (!tmp) {
	rb_raise(rb_eLoadError, "No such file to load -- %s", RSTRING(fname)->ptr);
    }
    fname = tmp;

    ruby_errinfo = Qnil;	/* ensure */
    PUSH_VARS();
    PUSH_CLASS();
    wrapper = ruby_wrapper;
    ruby_cref = top_cref;
    if (!wrap) {
	rb_secure(4);		/* should alter global state */
	ruby_class = rb_cObject;
	ruby_wrapper = 0;
    }
    else {
	/* load in anonymous module as toplevel */
	ruby_class = ruby_wrapper = rb_module_new();
	self = rb_obj_clone(ruby_top_self);
	rb_extend_object(self, ruby_wrapper);
	PUSH_CREF(ruby_wrapper);
    }
    PUSH_ITER(ITER_NOT);
    PUSH_FRAME();
    ruby_frame->last_func = 0;
    ruby_frame->last_class = 0;
    ruby_frame->self = self;
    ruby_frame->cbase = (VALUE)rb_node_newnode(NODE_CREF,ruby_class,0,0);
    PUSH_SCOPE();
    /* default visibility is private at loading toplevel */
    SCOPE_SET(SCOPE_PRIVATE);
    PUSH_TAG(PROT_NONE);
    state = EXEC_TAG();
    last_func = ruby_frame->last_func;
    if (state == 0) {
	NODE *node;
	volatile int critical;

	DEFER_INTS;
	ruby_in_eval++;
	critical = rb_thread_critical;
	rb_thread_critical = Qtrue;
	rb_load_file(RSTRING(fname)->ptr);
	ruby_in_eval--;
	node = ruby_eval_tree;
	rb_thread_critical = critical;
	if (ruby_nerrs == 0) {
	    eval_node(self, node);
	}
    }
    ALLOW_INTS;
    ruby_frame->last_func = last_func;
    if (ruby_scope->flags == SCOPE_ALLOCA && ruby_class == rb_cObject) {
	if (ruby_scope->local_tbl) /* toplevel was empty */
	    free(ruby_scope->local_tbl);
    }
    POP_TAG();
    ruby_cref = saved_cref;
    POP_SCOPE();
    POP_FRAME();
    POP_ITER();
    POP_CLASS();
    POP_VARS();
    ruby_wrapper = wrapper;
    if (ruby_nerrs > 0) {
	ruby_nerrs = 0;
	rb_exc_raise(ruby_errinfo);
    }
    if (state) jump_tag_but_local_jump(state);
    if (!NIL_P(ruby_errinfo))	/* exception during load */
	rb_exc_raise(ruby_errinfo);
}

void
rb_load_protect(fname, wrap, state)
    VALUE fname;
    int wrap;
    int *state;
{
    int status;

    PUSH_TAG(PROT_NONE);
    if ((status = EXEC_TAG()) == 0) {
	rb_load(fname, wrap);
    }
    POP_TAG();
    if (state) *state = status;
}

static VALUE
rb_f_load(argc, argv)
    int argc;
    VALUE *argv;
{
    VALUE fname, wrap;

    rb_scan_args(argc, argv, "11", &fname, &wrap);
    rb_load(fname, RTEST(wrap));
    return Qtrue;
}

VALUE ruby_dln_librefs;
static VALUE rb_features;
static st_table *loading_tbl;

static int
rb_feature_p(feature, wait)
    const char *feature;
    int wait;
{
    VALUE v;
    char *f;
    long i, len = strlen(feature);

    for (i = 0; i < RARRAY(rb_features)->len; ++i) {
	v = RARRAY(rb_features)->ptr[i];
	f = StringValuePtr(v);
	if (strcmp(f, feature) == 0) {
	    goto load_wait;
	}
	if (strncmp(f, feature, len) == 0) {
	    if (strcmp(f+len, ".so") == 0) {
		return Qtrue;
	    }
	    if (strcmp(f+len, ".rb") == 0) {
		if (wait) goto load_wait;
		return Qtrue;
	    }
	}
    }
    return Qfalse;

  load_wait:
    if (loading_tbl) {
	char *ext = strrchr(f, '.');
	if (ext && strcmp(ext, ".rb") == 0) {
	    rb_thread_t th;

	    while (st_lookup(loading_tbl, (st_data_t)f, (st_data_t *)&th)) {
		if (th == curr_thread) {
		    return Qtrue;
		}
		CHECK_INTS;
		rb_thread_schedule();
	    }
	}
    }
    return Qtrue;
}

static const char *const loadable_ext[] = {
    ".rb", DLEXT,
#ifdef DLEXT2
    DLEXT2,
#endif
    0
};

int
rb_provided(feature)
    const char *feature;
{
    VALUE f = rb_str_new2(feature);

    if (strrchr(feature, '.') == 0) {
	if (rb_find_file_ext(&f, loadable_ext) == 0) {
	    return rb_feature_p(feature, Qfalse);
	}
    }
    return rb_feature_p(RSTRING(f)->ptr, Qfalse);
}

static void
rb_provide_feature(feature)
    VALUE feature;
{
    rb_ary_push(rb_features, feature);
}

void
rb_provide(feature)
    const char *feature;
{
    rb_provide_feature(rb_str_new2(feature));
}

NORETURN(static void load_failed _((VALUE)));
static VALUE load_dyna _((VALUE, VALUE));
static VALUE load_rb _((VALUE, VALUE));

VALUE
rb_f_require(obj, fname)
    VALUE obj, fname;
{
    VALUE feature, tmp;
    char *ext; /* OK */

    SafeStringValue(fname);
    ext = strrchr(RSTRING(fname)->ptr, '.');
    if (ext && strchr(ext, '/')) ext = 0;
    if (ext) {
	if (strcmp(".rb", ext) == 0) {
	    feature = rb_str_dup(fname);
	    tmp = rb_find_file(fname);
	    if (tmp) {
		return load_rb(feature, tmp);
	    }
	    load_failed(fname);
	}
	else if (strcmp(".so", ext) == 0 || strcmp(".o", ext) == 0) {
	    tmp = rb_str_new(RSTRING(fname)->ptr, ext-RSTRING(fname)->ptr);
#ifdef DLEXT2
	    if (rb_find_file_ext(&tmp, loadable_ext+1)) {
		return load_dyna(tmp, rb_find_file(tmp));
	    }
#else
	    feature = tmp;
	    rb_str_cat2(tmp, DLEXT);
	    tmp = rb_find_file(tmp);
	    if (tmp) {
		return load_dyna(feature, tmp);
	    }
#endif
	}
	else if (strcmp(DLEXT, ext) == 0) {
	    tmp = rb_find_file(fname);
	    if (tmp) {
		return load_dyna(fname, tmp);
	    }
	}
#ifdef DLEXT2
	else if (strcmp(DLEXT2, ext) == 0) {
	    tmp = rb_find_file(fname);
	    if (tmp) {
		return load_dyna(fname, tmp);
	    }
	}
#endif
    }
    tmp = fname;
    switch (rb_find_file_ext(&tmp, loadable_ext)) {
      case 0:
	break;

      case 1:
	return load_rb(tmp, tmp);

      default:
	return load_dyna(tmp, rb_find_file(tmp));
    }
    if (!rb_feature_p(RSTRING(fname)->ptr, Qfalse))
	load_failed(fname);
    return Qfalse;
}

static void
load_failed(fname)
    VALUE fname;
{
    rb_raise(rb_eLoadError, "No such file to load -- %s", RSTRING(fname)->ptr);
}

static VALUE
load_dyna(feature, fname)
    VALUE feature, fname;
{
    int state;

    if (rb_feature_p(RSTRING(feature)->ptr, Qfalse))
	return Qfalse;
    rb_provide_feature(feature);
    {
	int volatile old_vmode = scope_vmode;
	NODE *const volatile old_node = ruby_current_node;
	const volatile ID old_func = ruby_frame->last_func;

	ruby_current_node = 0;
	ruby_sourcefile = rb_source_filename(RSTRING(fname)->ptr);
	ruby_sourceline = 0;
	ruby_frame->last_func = 0;
	PUSH_TAG(PROT_NONE);
	if ((state = EXEC_TAG()) == 0) {
	    void *handle;

	    SCOPE_SET(SCOPE_PUBLIC);
	    handle = dln_load(RSTRING(fname)->ptr);
	    rb_ary_push(ruby_dln_librefs, LONG2NUM((long)handle));
	}
	POP_TAG();
	ruby_current_node = old_node;
	ruby_set_current_source();
	ruby_frame->last_func = old_func;
	SCOPE_SET(old_vmode);
    }
    if (state) JUMP_TAG(state);
    ruby_errinfo = Qnil;

    return Qtrue;
}

static VALUE
load_rb(feature, fname)
    VALUE feature, fname;
{
    int state;
    char *ftptr;
    volatile int safe = ruby_safe_level;

    if (rb_feature_p(RSTRING(feature)->ptr, Qtrue))
	return Qfalse;
    ruby_safe_level = 0;
    rb_provide_feature(feature);
    /* loading ruby library should be serialized. */
    if (!loading_tbl) {
	loading_tbl = st_init_strtable();
    }
    /* partial state */
    ftptr = ruby_strdup(RSTRING(feature)->ptr);
    st_insert(loading_tbl, (st_data_t)ftptr, (st_data_t)curr_thread);

    PUSH_TAG(PROT_NONE);
    if ((state = EXEC_TAG()) == 0) {
	rb_load(fname, 0);
    }
    POP_TAG();
    st_delete(loading_tbl, (st_data_t *)&ftptr, 0); /* loading done */
    free(ftptr);
    ruby_safe_level = safe;
    if (state) JUMP_TAG(state);

    return Qtrue;
}

VALUE
rb_require(fname)
    const char *fname;
{
    return rb_f_require(Qnil, rb_str_new2(fname));
}

static void
secure_visibility(self)
    VALUE self;
{
    if (ruby_safe_level >= 4 && !OBJ_TAINTED(self)) {
	rb_raise(rb_eSecurityError, "Insecure: can't change method visibility");
    }
}

static void
set_method_visibility(self, argc, argv, ex)
    VALUE self;
    int argc;
    VALUE *argv;
    ID ex;
{
    int i;

    secure_visibility(self);
    for (i=0; i<argc; i++) {
	rb_export_method(self, rb_to_id(argv[i]), ex);
    }
    rb_clear_cache_by_class(self);
}

static VALUE
rb_mod_public(argc, argv, module)
    int argc;
    VALUE *argv;
    VALUE module;
{
    secure_visibility(module);
    if (argc == 0) {
	SCOPE_SET(SCOPE_PUBLIC);
    }
    else {
	set_method_visibility(module, argc, argv, NOEX_PUBLIC);
    }
    return module;
}

static VALUE
rb_mod_protected(argc, argv, module)
    int argc;
    VALUE *argv;
    VALUE module;
{
    secure_visibility(module);
    if (argc == 0) {
	SCOPE_SET(SCOPE_PROTECTED);
    }
    else {
	set_method_visibility(module, argc, argv, NOEX_PROTECTED);
    }
    return module;
}

static VALUE
rb_mod_private(argc, argv, module)
    int argc;
    VALUE *argv;
    VALUE module;
{
    secure_visibility(module);
    if (argc == 0) {
	SCOPE_SET(SCOPE_PRIVATE);
    }
    else {
	set_method_visibility(module, argc, argv, NOEX_PRIVATE);
    }
    return module;
}

static VALUE
rb_mod_public_method(argc, argv, obj)
    int argc;
    VALUE *argv;
    VALUE obj;
{
    set_method_visibility(CLASS_OF(obj), argc, argv, NOEX_PUBLIC);
    return obj;
}

static VALUE
rb_mod_private_method(argc, argv, obj)
    int argc;
    VALUE *argv;
    VALUE obj;
{
    set_method_visibility(CLASS_OF(obj), argc, argv, NOEX_PRIVATE);
    return obj;
}

static VALUE
top_public(argc, argv)
    int argc;
    VALUE *argv;
{
    return rb_mod_public(argc, argv, rb_cObject);
}

static VALUE
top_private(argc, argv)
    int argc;
    VALUE *argv;
{
    return rb_mod_private(argc, argv, rb_cObject);
}

static VALUE
rb_mod_modfunc(argc, argv, module)
    int argc;
    VALUE *argv;
    VALUE module;
{
    int i;
    ID id;
    NODE *body;

    if (TYPE(module) != T_MODULE) {
	rb_raise(rb_eTypeError, "module_function must be called for modules");
    }

    secure_visibility(module);
    if (argc == 0) {
	SCOPE_SET(SCOPE_MODFUNC);
	return module;
    }

    set_method_visibility(module, argc, argv, NOEX_PRIVATE);
    for (i=0; i<argc; i++) {
	VALUE m = module;

	id = rb_to_id(argv[i]);
	for (;;) {
	    body = search_method(m, id, &m);
	    if (body == 0 || body->nd_body == 0) {
		rb_bug("undefined method `%s'; can't happen", rb_id2name(id));
	    }
	    if (nd_type(body->nd_body) != NODE_ZSUPER) {
		break;		/* normal case: need not to follow 'super' link */
	    }
	    m = RCLASS(m)->super;
	}
	rb_add_method(rb_singleton_class(module), id, body->nd_body, NOEX_PUBLIC);
    }
    return module;
}

static VALUE
rb_mod_append_features(module, include)
    VALUE module, include;
{
    switch (TYPE(include)) {
      case T_CLASS:
      case T_MODULE:
	break;
      default:
	Check_Type(include, T_CLASS);
	break;
    }
    rb_include_module(include, module);

    return module;
}

static VALUE
rb_mod_include(argc, argv, module)
    int argc;
    VALUE *argv;
    VALUE module;
{
    while (argc--) {
	VALUE m = argv[argc];

	Check_Type(m, T_MODULE);
	rb_funcall(m, rb_intern("append_features"), 1, module);
	rb_funcall(m, rb_intern("included"), 1, module);
    }
    return module;
}

void
rb_obj_call_init(obj, argc, argv)
    VALUE obj;
    int argc;
    VALUE *argv;
{
    PUSH_ITER(rb_block_given_p()?ITER_PRE:ITER_NOT);
    rb_funcall2(obj, init, argc, argv);
    POP_ITER();
}

void
rb_extend_object(obj, module)
    VALUE obj, module;
{
    rb_include_module(rb_singleton_class(obj), module);
}

static VALUE
rb_mod_extend_object(mod, obj)
    VALUE mod, obj;
{
    rb_extend_object(obj, mod);
    return obj;
}

static VALUE
rb_obj_extend(argc, argv, obj)
    int argc;
    VALUE *argv;
    VALUE obj;
{
    int i;

    if (argc == 0) {
	rb_raise(rb_eArgError, "wrong number of arguments(0 for 1)");
    }
    for (i=0; i<argc; i++) Check_Type(argv[i], T_MODULE);
    while (argc--) {
	rb_funcall(argv[argc], rb_intern("extend_object"), 1, obj);
    }
    return obj;
}

static VALUE
top_include(argc, argv, self)
    int argc;
    VALUE *argv;
    VALUE self;
{
    rb_secure(4);
    if (ruby_wrapper) {
	rb_warn("main#include in the wrapped load is effective only for toplevel");
	return rb_obj_extend(argc, argv, self);
    }
    else {
	return rb_mod_include(argc, argv, rb_cObject);
    }
}

VALUE rb_f_trace_var();
VALUE rb_f_untrace_var();

static void
errinfo_setter(val, id, var)
    VALUE val;
    ID id;
    VALUE *var;
{
    if (!NIL_P(val) && !rb_obj_is_kind_of(val, rb_eException)) {
	rb_raise(rb_eTypeError, "assigning non-exception to $!");
    }
    *var = val;
}

static VALUE
errat_getter(id)
    ID id;
{
    return get_backtrace(ruby_errinfo);
}

static void
errat_setter(val, id, var)
    VALUE val;
    ID id;
    VALUE *var;
{
    if (NIL_P(ruby_errinfo)) {
	rb_raise(rb_eArgError, "$! not set");
    }
    set_backtrace(ruby_errinfo, val);
}

static VALUE
rb_f_local_variables()
{
    ID *tbl;
    int n, i;
    VALUE ary = rb_ary_new();
    struct RVarmap *vars;

    tbl = ruby_scope->local_tbl;
    if (tbl) {
	n = *tbl++;
	for (i=2; i<n; i++) {	/* skip first 2 ($_ and $~) */
	    if (!rb_is_local_id(tbl[i])) continue; /* skip flip states */
	    rb_ary_push(ary, rb_str_new2(rb_id2name(tbl[i])));
	}
    }

    vars = ruby_dyna_vars;
    while (vars) {
	if (vars->id && rb_is_local_id(vars->id)) { /* skip $_, $~ and flip states */
	    rb_ary_push(ary, rb_str_new2(rb_id2name(vars->id)));
	}
	vars = vars->next;
    }

    return ary;
}

static VALUE rb_f_catch _((VALUE,VALUE));
NORETURN(static VALUE rb_f_throw _((int,VALUE*)));

struct end_proc_data {
    void (*func)();
    VALUE data;
    struct end_proc_data *next;
};

static struct end_proc_data *end_procs, *ephemeral_end_procs;

void
rb_set_end_proc(func, data)
    void (*func) _((VALUE));
    VALUE data;
{
    struct end_proc_data *link = ALLOC(struct end_proc_data);
    struct end_proc_data **list;

    if (ruby_wrapper) list = &ephemeral_end_procs;
    else              list = &end_procs;
    link->next = *list;
    link->func = func;
    link->data = data;
    *list = link;
}

void
rb_mark_end_proc()
{
    struct end_proc_data *link;

    link = end_procs;
    while (link) {
	rb_gc_mark(link->data);
	link = link->next;
    }
    link = ephemeral_end_procs;
    while (link) {
	rb_gc_mark(link->data);
	link = link->next;
    }
}

static void call_end_proc _((VALUE data));

static void
call_end_proc(data)
    VALUE data;
{
    PUSH_ITER(ITER_NOT);
    PUSH_FRAME();
    ruby_frame->self = ruby_frame->prev->self;
    ruby_frame->last_func = 0;
    ruby_frame->last_class = 0;
    proc_invoke(data, rb_ary_new2(0), Qundef, 0);
    POP_FRAME();
    POP_ITER();
}

static void
rb_f_END()
{
    PUSH_FRAME();
    ruby_frame->argc = 0;
    rb_set_end_proc(call_end_proc, rb_block_proc());
    POP_FRAME();
}

static VALUE
rb_f_at_exit()
{
    VALUE proc;

    proc = rb_block_proc();
    rb_set_end_proc(call_end_proc, proc);
    return proc;
}

void
rb_exec_end_proc()
{
    struct end_proc_data *link, *save;
    int status;

    save = link = end_procs;
    while (link) {
	PUSH_TAG(PROT_NONE);
	if ((status = EXEC_TAG()) == 0) {
	    (*link->func)(link->data);
	}
	POP_TAG();
	if (status) {
	    error_handle(status);
	}
	link = link->next;
    }
    link = end_procs;
    while (link != save) {
	PUSH_TAG(PROT_NONE);
	if ((status = EXEC_TAG()) == 0) {
	    (*link->func)(link->data);
	}
	POP_TAG();
	if (status) {
	    error_handle(status);
	}
	link = link->next;
    }
    while (ephemeral_end_procs) {
	link = ephemeral_end_procs;
	ephemeral_end_procs = link->next;
	PUSH_TAG(PROT_NONE);
	if ((status = EXEC_TAG()) == 0) {
	    (*link->func)(link->data);
	}
	POP_TAG();
	if (status) {
	    error_handle(status);
	}
	free(link);
    }
}

void
Init_eval()
{
    init = rb_intern("initialize");
    eqq = rb_intern("===");
    each = rb_intern("each");

    aref = rb_intern("[]");
    aset = rb_intern("[]=");
    match = rb_intern("=~");
    missing = rb_intern("method_missing");
    added = rb_intern("method_added");
    singleton_added = rb_intern("singleton_method_added");
    removed = rb_intern("method_removed");
    singleton_removed = rb_intern("singleton_method_removed");
    undefined = rb_intern("method_undefined");
    singleton_undefined = rb_intern("singleton_method_undefined");

    __id__ = rb_intern("__id__");
    __send__ = rb_intern("__send__");

    rb_global_variable((VALUE*)&top_scope);
    rb_global_variable((VALUE*)&ruby_eval_tree_begin);

    rb_global_variable((VALUE*)&ruby_eval_tree);
    rb_global_variable((VALUE*)&ruby_dyna_vars);

    rb_define_virtual_variable("$@", errat_getter, errat_setter);
    rb_define_hooked_variable("$!", &ruby_errinfo, 0, errinfo_setter);

    rb_define_global_function("eval", rb_f_eval, -1);
    rb_define_global_function("iterator?", rb_f_block_given_p, 0);
    rb_define_global_function("block_given?", rb_f_block_given_p, 0);
    rb_define_global_function("method_missing", rb_method_missing, -1);
    rb_define_global_function("loop", rb_f_loop, 0);

    rb_define_method(rb_mKernel, "respond_to?", rb_obj_respond_to, -1);

    rb_define_global_function("raise", rb_f_raise, -1);
    rb_define_global_function("fail", rb_f_raise, -1);

    rb_define_global_function("caller", rb_f_caller, -1);

    rb_define_global_function("exit", rb_f_exit, -1);
    rb_define_global_function("abort", rb_f_abort, -1);

    rb_define_global_function("at_exit", rb_f_at_exit, 0);

    rb_define_global_function("catch", rb_f_catch, 1);
    rb_define_global_function("throw", rb_f_throw, -1);
    rb_define_global_function("global_variables", rb_f_global_variables, 0);
    rb_define_global_function("local_variables", rb_f_local_variables, 0);

    rb_define_method(rb_mKernel, "send", rb_f_send, -1);
    rb_define_method(rb_mKernel, "__send__", rb_f_send, -1);
    rb_define_method(rb_mKernel, "instance_eval", rb_obj_instance_eval, -1);

    rb_define_private_method(rb_cModule, "append_features", rb_mod_append_features, 1);
    rb_define_private_method(rb_cModule, "extend_object", rb_mod_extend_object, 1);
    rb_define_private_method(rb_cModule, "include", rb_mod_include, -1);
    rb_define_private_method(rb_cModule, "public", rb_mod_public, -1);
    rb_define_private_method(rb_cModule, "protected", rb_mod_protected, -1);
    rb_define_private_method(rb_cModule, "private", rb_mod_private, -1);
    rb_define_private_method(rb_cModule, "module_function", rb_mod_modfunc, -1);
    rb_define_method(rb_cModule, "method_defined?", rb_mod_method_defined, 1);
    rb_define_method(rb_cModule, "public_method_defined?", rb_mod_public_method_defined, 1);
    rb_define_method(rb_cModule, "private_method_defined?", rb_mod_private_method_defined, 1);
    rb_define_method(rb_cModule, "protected_method_defined?", rb_mod_protected_method_defined, 1);
    rb_define_method(rb_cModule, "public_class_method", rb_mod_public_method, -1);
    rb_define_method(rb_cModule, "private_class_method", rb_mod_private_method, -1);
    rb_define_method(rb_cModule, "module_eval", rb_mod_module_eval, -1);
    rb_define_method(rb_cModule, "class_eval", rb_mod_module_eval, -1);

    rb_undef_method(rb_cClass, "module_function");

    rb_define_private_method(rb_cModule, "remove_method", rb_mod_remove_method, -1);
    rb_define_private_method(rb_cModule, "undef_method", rb_mod_undef_method, -1);
    rb_define_private_method(rb_cModule, "alias_method", rb_mod_alias_method, 2);
    rb_define_private_method(rb_cModule, "define_method", rb_mod_define_method, -1);

    rb_define_singleton_method(rb_cModule, "nesting", rb_mod_nesting, 0);
    rb_define_singleton_method(rb_cModule, "constants", rb_mod_s_constants, 0);

    rb_define_singleton_method(ruby_top_self, "include", top_include, -1);
    rb_define_singleton_method(ruby_top_self, "public", top_public, -1);
    rb_define_singleton_method(ruby_top_self, "private", top_private, -1);

    rb_define_method(rb_mKernel, "extend", rb_obj_extend, -1);

    rb_define_global_function("trace_var", rb_f_trace_var, -1);
    rb_define_global_function("untrace_var", rb_f_untrace_var, -1);

    rb_define_global_function("set_trace_func", set_trace_func, 1);
    rb_global_variable(&trace_func);

    rb_define_virtual_variable("$SAFE", safe_getter, safe_setter);
}

static VALUE
rb_mod_autoload(mod, sym, file)
    VALUE mod;
    VALUE sym;
    VALUE file;
{
    ID id = rb_to_id(sym);

    Check_SafeStr(file);
    rb_autoload(mod, id, RSTRING(file)->ptr);
    return Qnil;
}

static VALUE
rb_mod_autoload_p(mod, sym)
    VALUE mod, sym;
{
    return rb_autoload_p(mod, rb_to_id(sym));
}

static VALUE
rb_f_autoload(obj, sym, file)
    VALUE obj;
    VALUE sym;
    VALUE file;
{
    return rb_mod_autoload(ruby_class, sym, file);
}

static VALUE
rb_f_autoload_p(obj, sym)
    VALUE obj;
    VALUE sym;
{
    /* use ruby_class as same as rb_f_autoload. */
    return rb_mod_autoload_p(ruby_class, sym);
}

void
Init_load()
{
    rb_load_path = rb_ary_new();
    rb_define_readonly_variable("$:", &rb_load_path);
    rb_define_readonly_variable("$-I", &rb_load_path);
    rb_define_readonly_variable("$LOAD_PATH", &rb_load_path);

    rb_features = rb_ary_new();
    rb_define_readonly_variable("$\"", &rb_features);

    rb_define_global_function("load", rb_f_load, -1);
    rb_define_global_function("require", rb_f_require, 1);
    rb_define_method(rb_cModule, "autoload",  rb_mod_autoload,   2);
    rb_define_method(rb_cModule, "autoload?", rb_mod_autoload_p, 1);
    rb_define_global_function("autoload",  rb_f_autoload,   2);
    rb_define_global_function("autoload?", rb_f_autoload_p, 1);
    rb_global_variable(&ruby_wrapper);

    ruby_dln_librefs = rb_ary_new();
    rb_global_variable(&ruby_dln_librefs);
}

static void
scope_dup(scope)
    struct SCOPE *scope;
{
    ID *tbl;
    VALUE *vars;

    scope->flags |= SCOPE_DONT_RECYCLE;
    if (scope->flags & SCOPE_MALLOC) return;

    if (scope->local_tbl) {
	tbl = scope->local_tbl;
	vars = ALLOC_N(VALUE, tbl[0]+1);
	*vars++ = scope->local_vars[-1];
	MEMCPY(vars, scope->local_vars, VALUE, tbl[0]);
	scope->local_vars = vars;
	scope->flags |= SCOPE_MALLOC;
    }
}

static void
blk_mark(data)
    struct BLOCK *data;
{
    while (data) {
	rb_gc_mark_frame(&data->frame);
	rb_gc_mark((VALUE)data->scope);
	rb_gc_mark((VALUE)data->var);
	rb_gc_mark((VALUE)data->body);
	rb_gc_mark((VALUE)data->self);
	rb_gc_mark((VALUE)data->dyna_vars);
	rb_gc_mark((VALUE)data->klass);
	rb_gc_mark((VALUE)data->tag);
	rb_gc_mark(data->wrapper);
	rb_gc_mark(data->block_obj);
	data = data->prev;
    }
}

static void
blk_free(data)
    struct BLOCK *data;
{
    struct FRAME *frame;
    void *tmp;

    frame = data->frame.prev;
    while (frame) {
	if (frame->argc > 0 && (frame->flags & FRAME_MALLOC))
	    free(frame->argv);
	tmp = frame;
	frame = frame->prev;
	free(tmp);
    }
    while (data) {
	if (data->frame.argc > 0)
	    free(data->frame.argv);
	tmp = data;
	data = data->prev;
	free(tmp);
    }
}

static void
blk_copy_prev(block)
    struct BLOCK *block;
{
    struct BLOCK *tmp;
    struct RVarmap* vars;

    while (block->prev) {
	tmp = ALLOC_N(struct BLOCK, 1);
	MEMCPY(tmp, block->prev, struct BLOCK, 1);
	if (tmp->frame.argc > 0) {
	    tmp->frame.argv = ALLOC_N(VALUE, tmp->frame.argc);
	    MEMCPY(tmp->frame.argv, block->prev->frame.argv, VALUE, tmp->frame.argc);
	    tmp->frame.flags |= FRAME_MALLOC;
	}
	scope_dup(tmp->scope);
	tmp->tag->flags |= BLOCK_DYNAMIC;

	for (vars = tmp->dyna_vars; vars; vars = vars->next) {
	    if (FL_TEST(vars, DVAR_DONT_RECYCLE)) break;
	    FL_SET(vars, DVAR_DONT_RECYCLE);
	}

	block->prev = tmp;
	block = tmp;
    }
}

static void
frame_dup(frame)
    struct FRAME *frame;
{
    VALUE *argv;
    struct FRAME *tmp;

    for (;;) {
	if (frame->argc > 0) {
	    argv = ALLOC_N(VALUE, frame->argc);
	    MEMCPY(argv, frame->argv, VALUE, frame->argc);
	    frame->argv = argv;
	    frame->flags |= FRAME_MALLOC;
	}
	frame->tmp = 0;		/* should not preserve tmp */
	if (!frame->prev) break;
	tmp = ALLOC(struct FRAME);
	*tmp = *frame->prev;
	frame->prev = tmp;
	frame = tmp;
    }
}

static VALUE
proc_clone(self)
    VALUE self;
{
    struct BLOCK *orig, *data;
    VALUE bind;

    Data_Get_Struct(self, struct BLOCK, orig);
    bind = Data_Make_Struct(rb_obj_class(self),struct BLOCK,blk_mark,blk_free,data);
    CLONESETUP(bind, self);
    MEMCPY(data, orig, struct BLOCK, 1);
    frame_dup(&data->frame);

    if (data->iter) {
	blk_copy_prev(data);
    }
    else {
	data->prev = 0;
    }

    return bind;
}

static VALUE
rb_f_binding(self)
    VALUE self;
{
    struct BLOCK *data, *p;
    struct RVarmap *vars;
    VALUE bind;

    PUSH_BLOCK(0,0);
    bind = Data_Make_Struct(rb_cBinding,struct BLOCK,blk_mark,blk_free,data);
    *data = *ruby_block;

    data->orig_thread = rb_thread_current();
    data->wrapper = ruby_wrapper;
    data->iter = rb_f_block_given_p();
    frame_dup(&data->frame);
    if (ruby_frame->prev) {
	data->frame.last_func = ruby_frame->prev->last_func;
	data->frame.last_class = ruby_frame->prev->last_class;
    }

    if (data->iter) {
	blk_copy_prev(data);
    }
    else {
	data->prev = 0;
    }
    data->flags |= BLOCK_DYNAMIC;
    data->tag->flags |= BLOCK_DYNAMIC;

    for (p = data; p; p = p->prev) {
	for (vars = p->dyna_vars; vars; vars = vars->next) {
	    if (FL_TEST(vars, DVAR_DONT_RECYCLE)) break;
	    FL_SET(vars, DVAR_DONT_RECYCLE);
	}
    }
    scope_dup(data->scope);
    POP_BLOCK();

    return bind;
}

#define PROC_T3    FL_USER1
#define PROC_T4    FL_USER2
#define PROC_TMAX  (FL_USER1|FL_USER2)
#define PROC_TMASK (FL_USER1|FL_USER2)

static void
proc_save_safe_level(data)
    VALUE data;
{
    if (OBJ_TAINTED(data)) {
	switch (ruby_safe_level) {
	  case 3:
	    FL_SET(data, PROC_T3);
	    break;
	  case 4:
	    FL_SET(data, PROC_T4);
	    break;
	  default:
	    if (ruby_safe_level > 4) {
		FL_SET(data, PROC_TMAX);
	    }
	    break;
	}
    }
}

static int
proc_get_safe_level(data)
    VALUE data;
{
    if (OBJ_TAINTED(data)) {
	switch (RBASIC(data)->flags & PROC_TMASK) {
	  case PROC_T3:
	    return 3;
	  case PROC_T4:
	    return 4;
	  case PROC_TMAX:
	    return 5;
	}
	return 3;
    }
    return 0;
}

static void
proc_set_safe_level(data)
    VALUE data;
{
    if (OBJ_TAINTED(data)) {
	ruby_safe_level = proc_get_safe_level(data);
    }
}

static VALUE
proc_alloc(klass, proc)
    VALUE klass;
    int proc;
{
    volatile VALUE block;
    struct BLOCK *data, *p;
    struct RVarmap *vars;

    if (!rb_block_given_p() && !rb_f_block_given_p()) {
	rb_raise(rb_eArgError, "tried to create Proc object without a block");
    }
    if (proc && !rb_block_given_p()) {
	rb_warn("tried to create Proc object without a block");
    }

    if (!proc && ruby_block->block_obj) {
	return ruby_block->block_obj;
    }
    block = Data_Make_Struct(klass, struct BLOCK, blk_mark, blk_free, data);
    *data = *ruby_block;

    data->orig_thread = rb_thread_current();
    data->wrapper = ruby_wrapper;
    data->iter = data->prev?Qtrue:Qfalse;
    frame_dup(&data->frame);
    if (data->iter) {
	blk_copy_prev(data);
    }
    else {
	data->prev = 0;
    }
    data->flags |= BLOCK_DYNAMIC;
    data->tag->flags |= BLOCK_DYNAMIC;

    for (p = data; p; p = p->prev) {
	for (vars = p->dyna_vars; vars; vars = vars->next) {
	    if (FL_TEST(vars, DVAR_DONT_RECYCLE)) break;
	    FL_SET(vars, DVAR_DONT_RECYCLE);
	}
    }
    scope_dup(data->scope);
    proc_save_safe_level(block);
    if (proc) {
	data->flags |= BLOCK_LAMBDA;
    }
    else {
	ruby_block->block_obj = block;
    }

    return block;
}

static VALUE
proc_s_new(argc, argv, klass)
    int argc;
    VALUE *argv;
    VALUE klass;
{
    VALUE block = proc_alloc(klass, Qfalse);

    rb_obj_call_init(block, argc, argv);
    return block;
}

VALUE
rb_block_proc()
{
    return proc_alloc(rb_cProc, Qfalse);
}

VALUE
rb_f_lambda()
{
    rb_warn("rb_f_lambda() is deprecated; use rb_block_proc() instead");
    return proc_alloc(rb_cProc, Qtrue);
}

static VALUE
proc_lambda()
{
    return proc_alloc(rb_cProc, Qtrue);
}

static int
block_orphan(data)
    struct BLOCK *data;
{
    if ((data->tag->flags & BLOCK_ORPHAN) &&
	(data->scope->flags & SCOPE_NOSTACK)) {
	return 1;
    }
    if (data->orig_thread != rb_thread_current()) {
	return 1;
    }
    return 0;
}

static VALUE
proc_invoke(proc, args, self, klass)
    VALUE proc, args;		/* OK */
    VALUE self, klass;
{
    struct BLOCK * volatile old_block;
    struct BLOCK _block;
    struct BLOCK *data;
    volatile VALUE result = Qnil;
    int state, incoming_state;
    volatile int orphan;
    volatile int safe = ruby_safe_level;
    volatile VALUE old_wrapper = ruby_wrapper;
    struct RVarmap * volatile old_dvars = ruby_dyna_vars;
    volatile int pcall;

    if (rb_block_given_p() && ruby_frame->last_func) {
	rb_warning("block for %s#%s is useless",
		   rb_obj_classname(proc),
		   rb_id2name(ruby_frame->last_func));
    }

    Data_Get_Struct(proc, struct BLOCK, data);
    orphan = block_orphan(data);
    pcall = data->flags & BLOCK_LAMBDA ? YIELD_PROC_CALL : 0;

    ruby_wrapper = data->wrapper;
    ruby_dyna_vars = data->dyna_vars;
    /* PUSH BLOCK from data */
    old_block = ruby_block;
    _block = *data;
    if (self != Qundef) _block.frame.self = self;
    if (klass) _block.frame.last_class = klass;
    ruby_block = &_block;

    PUSH_ITER(ITER_CUR);
    ruby_frame->iter = ITER_CUR;
    PUSH_TAG(PROT_NONE);
    state = EXEC_TAG();
    if (state == 0) {
	proc_set_safe_level(proc);
	result = rb_yield_0(args, self, self!=Qundef?CLASS_OF(self):0, pcall, Qtrue);
    }
    POP_TAG();

    POP_ITER();
    incoming_state = state;
    if (orphan || ruby_block->tag->dst == state) {
	state &= TAG_MASK;
    }
    ruby_block = old_block;
    ruby_wrapper = old_wrapper;
    ruby_dyna_vars = old_dvars;
    ruby_safe_level = safe;

    switch (state) {
      case 0:
	break;
      case TAG_RETRY:
	if (pcall || orphan) {
	    localjump_error("retry from proc-closure", Qnil, state);
	}
	/* fall through */
      case TAG_BREAK:
      case TAG_RETURN:
	if (pcall) {
	    result = prot_tag->retval;
	    break;
	}
	else if (orphan) {	/* orphan block */
	    char mesg[32];
	    snprintf(mesg, sizeof mesg, "%s from proc-closure",
		     state == TAG_BREAK ? "break" : "return");
	    localjump_error(mesg, prot_tag->retval, state);
	}
	else if (state == incoming_state) {
	    ruby_block->tag->dst = incoming_state;
	}
      default:
	JUMP_TAG(state);
    }
    return result;
}

static VALUE
proc_call(proc, args)
    VALUE proc, args;		/* OK */
{
    return proc_invoke(proc, args, Qundef, 0);
}

static VALUE bmcall _((VALUE, VALUE));
static VALUE method_arity _((VALUE));

static VALUE
proc_arity(proc)
    VALUE proc;
{
    struct BLOCK *data;
    NODE *list;
    int n;

    Data_Get_Struct(proc, struct BLOCK, data);
    if (data->var == 0) {
	if (data->body && nd_type(data->body) == NODE_IFUNC &&
	    data->body->nd_cfnc == bmcall) {
	    return method_arity(data->body->nd_tval);
	}
	return INT2FIX(-1);
    }
    if (data->var == (NODE*)1) return INT2FIX(0);
    if (data->var == (NODE*)2) return INT2FIX(0);
    switch (nd_type(data->var)) {
      default:
	return INT2FIX(1);
      case NODE_MASGN:
	list = data->var->nd_head;
	n = 0;
	while (list) {
	    n++;
	    list = list->nd_next;
	}
	if (data->var->nd_args) return INT2FIX(-n-1);
	return INT2FIX(n);
    }
}

static VALUE
proc_eq(self, other)
    VALUE self, other;
{
    struct BLOCK *data, *data2;

    if (self == other) return Qtrue;
    if (TYPE(other) != T_DATA) return Qfalse;
    if (RDATA(other)->dmark != (RUBY_DATA_FUNC)blk_mark) return Qfalse;
    if (CLASS_OF(self) != CLASS_OF(other)) return Qfalse;
    Data_Get_Struct(self, struct BLOCK, data);
    Data_Get_Struct(other, struct BLOCK, data2);
    if (data->tag == data2->tag) return Qtrue;
    return Qfalse;
}

static VALUE
proc_to_s(self, other)
    VALUE self, other;
{
    struct BLOCK *data;
    NODE *node;
    char *cname = rb_obj_classname(self);
    const int w = (SIZEOF_LONG * CHAR_BIT) / 4;
    long len = strlen(cname)+6+w; /* 6:tags 16:addr */
    VALUE str;

    Data_Get_Struct(self, struct BLOCK, data);
    if ((node = data->frame.node) || (node = data->body)) {
	len += strlen(node->nd_file) + 2 + (SIZEOF_LONG*CHAR_BIT-NODE_LSHIFT)/3;
	str = rb_str_new(0, len);
	sprintf(RSTRING(str)->ptr, "#<%s:0x%.*lx@%s:%d>", cname, w, (VALUE)data->tag,
		node->nd_file, nd_line(node));
    }
    else {
	str = rb_str_new(0, len);
	sprintf(RSTRING(str)->ptr, "#<%s:0x%.*lx>", cname, w, (VALUE)data->tag);
    }
    RSTRING(str)->len = strlen(RSTRING(str)->ptr);
    if (OBJ_TAINTED(self)) OBJ_TAINT(str);

    return str;
}

static VALUE
proc_to_self(self)
    VALUE self;
{
    return self;
}

static VALUE
proc_binding(proc)
    VALUE proc;
{
    struct BLOCK *orig, *data;
    VALUE bind;

    Data_Get_Struct(proc, struct BLOCK, orig);
    bind = Data_Make_Struct(rb_cBinding,struct BLOCK,blk_mark,blk_free,data);
    MEMCPY(data, orig, struct BLOCK, 1);
    frame_dup(&data->frame);

    if (data->iter) {
	blk_copy_prev(data);
    }
    else {
	data->prev = 0;
    }

    return bind;
}

static VALUE
block_pass(self, node)
    VALUE self;
    NODE *node;
{
    VALUE proc = rb_eval(self, node->nd_body);	/* OK */
    VALUE b;
    struct BLOCK * volatile old_block;
    struct BLOCK _block;
    struct BLOCK *data;
    volatile VALUE result = Qnil;
    int state;
    volatile int orphan;
    volatile int safe = ruby_safe_level;

    if (NIL_P(proc)) {
	PUSH_ITER(ITER_NOT);
	result = rb_eval(self, node->nd_iter);
	POP_ITER();
	return result;
    }
    if (!rb_obj_is_proc(proc)) {
	b = rb_check_convert_type(proc, T_DATA, "Proc", "to_proc");
	if (!rb_obj_is_proc(b)) {
	    rb_raise(rb_eTypeError, "wrong argument type %s (expected Proc)",
		     rb_obj_classname(proc));
	}
	proc = b;
    }

    if (ruby_safe_level >= 1 && OBJ_TAINTED(proc)) {
	if (ruby_safe_level > proc_get_safe_level(proc)) {
	    rb_raise(rb_eSecurityError, "Insecure: tainted block value");
	}
    }

    Data_Get_Struct(proc, struct BLOCK, data);
    orphan = block_orphan(data);

  retry:
    /* PUSH BLOCK from data */
    old_block = ruby_block;
    _block = *data;
    _block.outer = ruby_block;
    ruby_block = &_block;
    PUSH_ITER(ITER_PRE);
    ruby_frame->iter = ITER_PRE;

    PUSH_TAG(PROT_NONE);
    state = EXEC_TAG();
    if (state == 0) {
	proc_set_safe_level(proc);
	if (safe > ruby_safe_level)
	    ruby_safe_level = safe;
	result = rb_eval(self, node->nd_iter);
    }
    POP_TAG();
    POP_ITER();
    if (_block.tag->dst == state) {
	if (orphan) {
	    state &= TAG_MASK;
	}
	else {
	    struct BLOCK *ptr = old_block;

	    while (ptr) {
		if (ptr->scope == _block.scope) {
		    ptr->tag->dst = state;
		    break;
		}
		ptr = ptr->prev;
	    }
	    if (!ptr) {
		state &= TAG_MASK;
	    }
	}
    }
    ruby_block = old_block;
    ruby_safe_level = safe;

    switch (state) {/* escape from orphan block */
      case 0:
	break;
      case TAG_BREAK:
	result = prot_tag->retval;
	break;
      case TAG_RETRY:
	goto retry;
      case TAG_RETURN:
	if (orphan) {
	    localjump_error("return from proc-closure", prot_tag->retval, state);
	}
      default:
	JUMP_TAG(state);
    }

    return result;
}

struct METHOD {
    VALUE klass, rklass;
    VALUE recv;
    ID id, oid;
    NODE *body;
};

static void
bm_mark(data)
    struct METHOD *data;
{
    rb_gc_mark(data->rklass);
    rb_gc_mark(data->klass);
    rb_gc_mark(data->recv);
    rb_gc_mark((VALUE)data->body);
}

static VALUE
mnew(klass, obj, id, mklass)
    VALUE klass, obj, mklass;
    ID id;
{
    VALUE method;
    NODE *body;
    int noex;
    struct METHOD *data;
    VALUE rklass = klass;
    ID oid = id;

  again:
    if ((body = rb_get_method_body(&klass, &id, &noex)) == 0) {
	print_undef(rklass, oid);
    }

    if (nd_type(body) == NODE_ZSUPER) {
	klass = RCLASS(klass)->super;
	goto again;
    }

    while (rklass != klass &&
	   (FL_TEST(rklass, FL_SINGLETON) || TYPE(rklass) == T_ICLASS)) {
	rklass = RCLASS(rklass)->super;
    }
    if (TYPE(klass) == T_ICLASS) klass = RBASIC(klass)->klass;
    method = Data_Make_Struct(mklass, struct METHOD, bm_mark, free, data);
    data->klass = klass;
    data->recv = obj;
    data->id = id;
    data->body = body;
    data->rklass = rklass;
    data->oid = oid;
    OBJ_INFECT(method, klass);

    return method;
}

static VALUE
method_eq(method, other)
    VALUE method, other;
{
    struct METHOD *m1, *m2;

    if (TYPE(other) != T_DATA || RDATA(other)->dmark != (RUBY_DATA_FUNC)bm_mark)
	return Qfalse;
    if (CLASS_OF(method) != CLASS_OF(other))
	return Qfalse;

    Data_Get_Struct(method, struct METHOD, m1);
    Data_Get_Struct(other, struct METHOD, m2);

    if (m1->klass != m2->klass || m1->rklass != m2->rklass ||
	m1->recv != m2->recv || m1->body != m2->body)
	return Qfalse;

    return Qtrue;
}

static VALUE
method_unbind(obj)
    VALUE obj;
{
    VALUE method;
    struct METHOD *orig, *data;

    Data_Get_Struct(obj, struct METHOD, orig);
    method = Data_Make_Struct(rb_cUnboundMethod, struct METHOD, bm_mark, free, data);
    data->klass = orig->klass;
    data->recv = Qundef;
    data->id = orig->id;
    data->body = orig->body;
    data->rklass = orig->rklass;
    data->oid = orig->oid;
    OBJ_INFECT(method, obj);

    return method;
}

static VALUE
rb_obj_method(obj, vid)
    VALUE obj;
    VALUE vid;
{
    return mnew(CLASS_OF(obj), obj, rb_to_id(vid), rb_cMethod);
}

static VALUE
rb_mod_method(mod, vid)
    VALUE mod;
    VALUE vid;
{
    return mnew(mod, Qundef, rb_to_id(vid), rb_cUnboundMethod);
}

static VALUE
method_clone(self)
    VALUE self;
{
    VALUE clone;
    struct METHOD *orig, *data;

    Data_Get_Struct(self, struct METHOD, orig);
    clone = Data_Make_Struct(CLASS_OF(self),struct METHOD, bm_mark, free, data);
    CLONESETUP(clone, self);
    *data = *orig;

    return clone;
}

static VALUE
method_call(argc, argv, method)
    int argc;
    VALUE *argv;
    VALUE method;
{
    VALUE result;	/* OK */
    struct METHOD *data;
    int state;
    volatile int safe = ruby_safe_level;

    Data_Get_Struct(method, struct METHOD, data);
    if (data->recv == Qundef) {
	rb_raise(rb_eTypeError, "you cannot call unbound method; bind first");
    }
    PUSH_ITER(rb_block_given_p()?ITER_PRE:ITER_NOT);
    PUSH_TAG(PROT_NONE);
    if (OBJ_TAINTED(method) && ruby_safe_level < 4) {
	ruby_safe_level = 4;
    }
    if ((state = EXEC_TAG()) == 0) {
	result = rb_call0(data->klass,data->recv,data->id,data->oid,argc,argv,data->body,0);
    }
    POP_TAG();
    POP_ITER();
    ruby_safe_level = safe;
    if (state) JUMP_TAG(state);
    return result;
}

static VALUE
umethod_bind(method, recv)
    VALUE method, recv;
{
    struct METHOD *data, *bound;

    Data_Get_Struct(method, struct METHOD, data);
    if (data->rklass != CLASS_OF(recv)) {
	if (FL_TEST(data->rklass, FL_SINGLETON)) {
	    rb_raise(rb_eTypeError, "singleton method called for a different object");
	}
	if (FL_TEST(CLASS_OF(recv), FL_SINGLETON) &&
	    st_lookup(RCLASS(CLASS_OF(recv))->m_tbl, data->oid, 0)) {
	    rb_raise(rb_eTypeError, "method `%s' overridden", rb_id2name(data->oid));
	}
	if(!rb_obj_is_kind_of(recv, data->rklass)) {
	    rb_raise(rb_eTypeError, "bind argument must be an instance of %s",
		     rb_class2name(data->rklass));
	}
    }

    method = Data_Make_Struct(rb_cMethod,struct METHOD,bm_mark,free,bound);
    *bound = *data;
    bound->recv = recv;
    bound->rklass = CLASS_OF(recv);

    return method;
}

static VALUE
method_arity(method)
    VALUE method;
{
    struct METHOD *data;
    NODE *body;
    int n;

    Data_Get_Struct(method, struct METHOD, data);

    body = data->body;
    switch (nd_type(body)) {
      case NODE_CFUNC:
	if (body->nd_argc < 0) return INT2FIX(-1);
	return INT2FIX(body->nd_argc);
      case NODE_ZSUPER:
	return INT2FIX(-1);
      case NODE_ATTRSET:
	return INT2FIX(1);
      case NODE_IVAR:
	return INT2FIX(0);
      case NODE_BMETHOD:
      case NODE_DMETHOD:
       return proc_arity(method);
      default:
	body = body->nd_next;	/* skip NODE_SCOPE */
	if (nd_type(body) == NODE_BLOCK)
	    body = body->nd_head;
	if (!body) return INT2FIX(0);
	n = body->nd_cnt;
	if (body->nd_opt || body->nd_rest != -1)
	    n = -n-1;
	return INT2FIX(n);
    }
}

static VALUE
method_inspect(method)
    VALUE method;
{
    struct METHOD *data;
    VALUE str;
    const char *s;
    char *sharp = "#";

    Data_Get_Struct(method, struct METHOD, data);
    str = rb_str_buf_new2("#<");
    s = rb_obj_classname(method);
    rb_str_buf_cat2(str, s);
    rb_str_buf_cat2(str, ": ");

    if (FL_TEST(data->klass, FL_SINGLETON)) {
	VALUE v = rb_iv_get(data->klass, "__attached__");

	if (data->recv == Qundef) {
	    rb_str_buf_append(str, rb_inspect(data->klass));
	}
	else if (data->recv == v) {
	    rb_str_buf_append(str, rb_inspect(v));
	    sharp = ".";
	}
	else {
	    rb_str_buf_append(str, rb_inspect(data->recv));
	    rb_str_buf_cat2(str, "(");
	    rb_str_buf_append(str, rb_inspect(v));
	    rb_str_buf_cat2(str, ")");
	    sharp = ".";
	}
    }
    else {
	rb_str_buf_cat2(str, rb_class2name(data->rklass));
	if (data->rklass != data->klass) {
	    rb_str_buf_cat2(str, "(");
	    rb_str_buf_cat2(str, rb_class2name(data->klass));
	    rb_str_buf_cat2(str, ")");
	}
    }
    rb_str_buf_cat2(str, sharp);
    rb_str_buf_cat2(str, rb_id2name(data->oid));
    rb_str_buf_cat2(str, ">");

    return str;
}

static VALUE
mproc(method)
    VALUE method;
{
    VALUE proc;

    /* emulate ruby's method call */
    PUSH_ITER(ITER_CUR);
    PUSH_FRAME();
    proc = rb_block_proc();
    POP_FRAME();
    POP_ITER();

    return proc;
}

static VALUE
bmcall(args, method)
    VALUE args, method;
{
    volatile VALUE a;

    a = svalue_to_avalue(args);
    return method_call(RARRAY(a)->len, RARRAY(a)->ptr, method);
}

VALUE
rb_proc_new(func, val)
    VALUE (*func)(ANYARGS);	/* VALUE yieldarg[, VALUE procarg] */
    VALUE val;
{
    struct BLOCK *data;
    VALUE proc = rb_iterate((VALUE(*)_((VALUE)))mproc, 0, func, val);

    Data_Get_Struct(proc, struct BLOCK, data);
    data->body->nd_state = YIELD_FUNC_AVALUE;
    return proc;
}

static VALUE
method_proc(method)
    VALUE method;
{
    VALUE proc;
    struct METHOD *mdata;
    struct BLOCK *bdata;

    proc = rb_iterate((VALUE(*)_((VALUE)))mproc, 0, bmcall, method);
    Data_Get_Struct(method, struct METHOD, mdata);
    Data_Get_Struct(proc, struct BLOCK, bdata);
    bdata->body->nd_file = mdata->body->nd_file;
    nd_set_line(bdata->body, nd_line(mdata->body));
    bdata->body->nd_state = YIELD_FUNC_SVALUE;

    return proc;
}

static VALUE
rb_obj_is_method(m)
    VALUE m;
{
    if (TYPE(m) == T_DATA && RDATA(m)->dmark == (RUBY_DATA_FUNC)bm_mark) {
	return Qtrue;
    }
    return Qfalse;
}

static VALUE
rb_mod_define_method(argc, argv, mod)
    int argc;
    VALUE *argv;
    VALUE mod;
{
    ID id;
    VALUE body;
    NODE *node;
    int noex;

    if (argc == 1) {
	id = rb_to_id(argv[0]);
	body = proc_lambda();
    }
    else if (argc == 2) {
	id = rb_to_id(argv[0]);
	body = argv[1];
	if (!rb_obj_is_method(body) && !rb_obj_is_proc(body)) {
	    rb_raise(rb_eTypeError, "wrong argument type %s (expected Proc/Method)",
		     rb_obj_classname(body));
	}
    }
    else {
	rb_raise(rb_eArgError, "wrong number of arguments(%d for 1)", argc);
    }
    if (RDATA(body)->dmark == (RUBY_DATA_FUNC)bm_mark) {
	node = NEW_DMETHOD(method_unbind(body));
    }
    else if (RDATA(body)->dmark == (RUBY_DATA_FUNC)blk_mark) {
	struct BLOCK *block;

	body = proc_clone(body);
	Data_Get_Struct(body, struct BLOCK, block);
	block->frame.last_func = id;
	block->frame.orig_func = id;
	block->frame.last_class = mod;
	node = NEW_BMETHOD(body);
    }
    else {
	/* type error */
	rb_raise(rb_eTypeError, "wrong argument type (expected Proc/Method)");
    }

    if (SCOPE_TEST(SCOPE_PRIVATE)) {
	noex = NOEX_PRIVATE;
    }
    else if (SCOPE_TEST(SCOPE_PROTECTED)) {
	noex = NOEX_PROTECTED;
    }
    else {
	noex = NOEX_PUBLIC;
    }
    rb_add_method(mod, id, node, noex);
    return body;
}

void
Init_Proc()
{
    rb_eLocalJumpError = rb_define_class("LocalJumpError", rb_eStandardError);
    rb_define_method(rb_eLocalJumpError, "exit_value", localjump_xvalue, 0);
    rb_define_method(rb_eLocalJumpError, "reason", localjump_reason, 0);

    exception_error = rb_exc_new2(rb_eFatal, "exception reentered");
    rb_global_variable(&exception_error);

    rb_eSysStackError = rb_define_class("SystemStackError", rb_eStandardError);
    sysstack_error = rb_exc_new2(rb_eSysStackError, "stack level too deep");
    rb_global_variable(&sysstack_error);

    rb_cProc = rb_define_class("Proc", rb_cObject);
    rb_undef_alloc_func(rb_cProc);
    rb_define_singleton_method(rb_cProc, "new", proc_s_new, -1);

    rb_define_method(rb_cProc, "clone", proc_clone, 0);
    rb_define_method(rb_cProc, "call", proc_call, -2);
    rb_define_method(rb_cProc, "arity", proc_arity, 0);
    rb_define_method(rb_cProc, "[]", proc_call, -2);
    rb_define_method(rb_cProc, "==", proc_eq, 1);
    rb_define_method(rb_cProc, "to_s", proc_to_s, 0);
    rb_define_method(rb_cProc, "to_proc", proc_to_self, 0);
    rb_define_method(rb_cProc, "binding", proc_binding, 0);

    rb_define_global_function("proc", proc_lambda, 0);
    rb_define_global_function("lambda", proc_lambda, 0);

    rb_cBinding = rb_define_class("Binding", rb_cObject);
    rb_undef_alloc_func(rb_cBinding);
    rb_undef_method(CLASS_OF(rb_cBinding), "new");
    rb_define_method(rb_cBinding, "clone", proc_clone, 0);
    rb_define_global_function("binding", rb_f_binding, 0);

    rb_cMethod = rb_define_class("Method", rb_cObject);
    rb_undef_alloc_func(rb_cMethod);
    rb_undef_method(CLASS_OF(rb_cMethod), "new");
    rb_define_method(rb_cMethod, "==", method_eq, 1);
    rb_define_method(rb_cMethod, "clone", method_clone, 0);
    rb_define_method(rb_cMethod, "call", method_call, -1);
    rb_define_method(rb_cMethod, "[]", method_call, -1);
    rb_define_method(rb_cMethod, "arity", method_arity, 0);
    rb_define_method(rb_cMethod, "inspect", method_inspect, 0);
    rb_define_method(rb_cMethod, "to_s", method_inspect, 0);
    rb_define_method(rb_cMethod, "to_proc", method_proc, 0);
    rb_define_method(rb_cMethod, "unbind", method_unbind, 0);
    rb_define_method(rb_mKernel, "method", rb_obj_method, 1);

    rb_cUnboundMethod = rb_define_class("UnboundMethod", rb_cObject);
    rb_undef_alloc_func(rb_cUnboundMethod);
    rb_undef_method(CLASS_OF(rb_cUnboundMethod), "new");
    rb_define_method(rb_cUnboundMethod, "==", method_eq, 1);
    rb_define_method(rb_cUnboundMethod, "clone", method_clone, 0);
    rb_define_method(rb_cUnboundMethod, "arity", method_arity, 0);
    rb_define_method(rb_cUnboundMethod, "inspect", method_inspect, 0);
    rb_define_method(rb_cUnboundMethod, "to_s", method_inspect, 0);
    rb_define_method(rb_cUnboundMethod, "bind", umethod_bind, 1);
    rb_define_method(rb_cModule, "instance_method", rb_mod_method, 1);
}

/* Windows SEH refers data on the stack. */
#undef SAVE_WIN32_EXCEPTION_LIST
#if defined _WIN32 || defined __CYGWIN__
#if defined __CYGWIN__
typedef unsigned long DWORD;
#endif

static inline DWORD
win32_get_exception_list()
{
    DWORD p;
# if defined _MSC_VER
#   ifdef _M_IX86
#   define SAVE_WIN32_EXCEPTION_LIST
    __asm mov eax, fs:[0];
    __asm mov p, eax;
#   endif
# elif defined __GNUC__
#   ifdef __i386__
#   define SAVE_WIN32_EXCEPTION_LIST
    __asm__("movl %%fs:0,%0" : "=r"(p));
#   endif
# elif defined __BORLANDC__
#   define SAVE_WIN32_EXCEPTION_LIST
    __emit__(0x64, 0xA1, 0, 0, 0, 0); /* mov eax, fs:[0] */
    p = _EAX;
# endif
    return p;
}

static inline void
win32_set_exception_list(p)
    DWORD p;
{
# if defined _MSC_VER
#   ifdef _M_IX86
    __asm mov eax, p;
    __asm mov fs:[0], eax;
#   endif
# elif defined __GNUC__
#   ifdef __i386__
    __asm__("movl %0,%%fs:0" :: "r"(p));
#   endif
# elif defined __BORLANDC__
    _EAX = p;
    __emit__(0x64, 0xA3, 0, 0, 0, 0); /* mov fs:[0], eax */
# endif
}

#if !defined SAVE_WIN32_EXCEPTION_LIST && !defined _WIN32_WCE
# error unsupported platform
#endif
#endif

static VALUE rb_eThreadError;

int rb_thread_pending = 0;

VALUE rb_cThread;

extern VALUE rb_last_status;

enum thread_status {
    THREAD_TO_KILL,
    THREAD_RUNNABLE,
    THREAD_STOPPED,
    THREAD_KILLED,
};

#define WAIT_FD		(1<<0)
#define WAIT_SELECT	(1<<1)
#define WAIT_TIME	(1<<2)
#define WAIT_JOIN	(1<<3)
#define WAIT_PID	(1<<4)

/* +infty, for this purpose */
#define DELAY_INFTY 1E30

#if !defined HAVE_PAUSE
# if defined _WIN32 && !defined __CYGWIN__
#  define pause() Sleep(INFINITE)
# else
#  define pause() sleep(0x7fffffff)
# endif
#endif

/* typedef struct thread * rb_thread_t; */

struct thread {
    struct thread *next, *prev;
    jmp_buf context;
#ifdef SAVE_WIN32_EXCEPTION_LIST
    DWORD win32_exception_list;
#endif

    VALUE result;

    int    stk_len;
    int   stk_max;
    VALUE*stk_ptr;
    VALUE*stk_pos;

    struct FRAME *frame;
    struct SCOPE *scope;
    struct RVarmap *dyna_vars;
    struct BLOCK *block;
    struct iter *iter;
    struct tag *tag;
    VALUE klass;
    VALUE wrapper;
    NODE *cref;

    int flags;		/* misc. states (vmode/rb_trap_immediate/raised) */

    NODE *node;

    int tracing;
    VALUE errinfo;
    VALUE last_status;
    VALUE last_line;
    VALUE last_match;

    int safe;

    enum thread_status status;
    int wait_for;
    int fd;
    fd_set readfds;
    fd_set writefds;
    fd_set exceptfds;
    int select_value;
    double delay;
    rb_thread_t join;

    int abort;
    int priority;
    VALUE thgroup;

    st_table *locals;

    VALUE thread;
};

#define THREAD_RAISED 0x200	 /* temporary flag */
#define THREAD_TERMINATING 0x400 /* persistent flag */
#define THREAD_FLAGS_MASK  0x400 /* mask for persistent flags */

#define FOREACH_THREAD_FROM(f,x) x = f; do { x = x->next;
#define END_FOREACH_FROM(f,x) } while (x != f)

#define FOREACH_THREAD(x) FOREACH_THREAD_FROM(curr_thread,x)
#define END_FOREACH(x)    END_FOREACH_FROM(curr_thread,x)

struct thread_status_t {
    NODE *node;

    int tracing;
    VALUE errinfo;
    VALUE last_status;
    VALUE last_line;
    VALUE last_match;

    int safe;

    enum thread_status status;
    int wait_for;
    int fd;
    fd_set readfds;
    fd_set writefds;
    fd_set exceptfds;
    int select_value;
    double delay;
    rb_thread_t join;
};

#define THREAD_COPY_STATUS(src, dst) (void)(	\
    (dst)->node = (src)->node,			\
						\
    (dst)->tracing = (src)->tracing,		\
    (dst)->errinfo = (src)->errinfo,		\
    (dst)->last_status = (src)->last_status,	\
    (dst)->last_line = (src)->last_line,	\
    (dst)->last_match = (src)->last_match,	\
						\
    (dst)->safe = (src)->safe,			\
						\
    (dst)->status = (src)->status,		\
    (dst)->wait_for = (src)->wait_for,		\
    (dst)->fd = (src)->fd,			\
    (dst)->readfds = (src)->readfds,		\
    (dst)->writefds = (src)->writefds,		\
    (dst)->exceptfds = (src)->exceptfds,	\
    (dst)->select_value = (src)->select_value,	\
    (dst)->delay = (src)->delay,		\
    (dst)->join = (src)->join,			\
    0)

static int
thread_set_raised()
{
    if (curr_thread->flags & THREAD_RAISED) return 1;
    curr_thread->flags |= THREAD_RAISED;
    return 0;
}

static int
thread_reset_raised()
{
    if (!(curr_thread->flags & THREAD_RAISED)) return 0;
    curr_thread->flags &= ~THREAD_RAISED;
    return 1;
}

static void rb_thread_ready _((rb_thread_t));

static VALUE
rb_trap_eval(cmd, sig)
    VALUE cmd;
    int sig;
{
    int state;
    VALUE val;			/* OK */
    volatile struct thread_status_t save;

    THREAD_COPY_STATUS(curr_thread, &save);
    rb_thread_ready(curr_thread);
    PUSH_TAG(PROT_NONE);
    PUSH_ITER(ITER_NOT);
    if ((state = EXEC_TAG()) == 0) {
	val = rb_eval_cmd(cmd, rb_ary_new3(1, INT2FIX(sig)), 0);
    }
    POP_ITER();
    POP_TAG();
    THREAD_COPY_STATUS(&save, curr_thread);

    if (state) {
	rb_trap_immediate = 0;
	JUMP_TAG(state);
    }

    if (curr_thread->status == THREAD_STOPPED) {
	rb_thread_schedule();
    }
    errno = EINTR;

    return val;
}

static const char *
thread_status_name(status)
    enum thread_status status;
{
    switch (status) {
      case THREAD_RUNNABLE:
	return "run";
      case THREAD_STOPPED:
	return "sleep";
      case THREAD_TO_KILL:
	return "aborting";
      case THREAD_KILLED:
	return "dead";
      default:
	return "unknown";
    }
}

/* $SAFE accessor */
void
rb_set_safe_level(level)
    int level;
{
    if (level > ruby_safe_level) {
	ruby_safe_level = level;
	curr_thread->safe = level;
    }
}

static VALUE
safe_getter()
{
    return INT2NUM(ruby_safe_level);
}

static void
safe_setter(val)
    VALUE val;
{
    int level = NUM2INT(val);

    if (level < ruby_safe_level) {
	rb_raise(rb_eSecurityError, "tried to downgrade safe level from %d to %d",
		 ruby_safe_level, level);
    }
    ruby_safe_level = level;
    curr_thread->safe = level;
}

/* Return the current time as a floating-point number */
static double
timeofday()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec + (double)tv.tv_usec * 1e-6;
}

#define STACK(addr) (th->stk_pos<(VALUE*)(addr) && (VALUE*)(addr)<th->stk_pos+th->stk_len)
#define ADJ(addr) (void*)(STACK(addr)?(((VALUE*)(addr)-th->stk_pos)+th->stk_ptr):(VALUE*)(addr))

static void
thread_mark(th)
    rb_thread_t th;
{
    struct FRAME *frame;
    struct BLOCK *block;

    rb_gc_mark(th->result);
    rb_gc_mark(th->thread);
    if (th->join) rb_gc_mark(th->join->thread);

    rb_gc_mark(th->klass);
    rb_gc_mark(th->wrapper);
    rb_gc_mark((VALUE)th->cref);

    rb_gc_mark((VALUE)th->scope);
    rb_gc_mark((VALUE)th->dyna_vars);
    rb_gc_mark(th->errinfo);
    rb_gc_mark(th->last_line);
    rb_gc_mark(th->last_match);
    rb_mark_tbl(th->locals);

    /* mark data in copied stack */
    if (th == curr_thread) return;
    if (th->status == THREAD_KILLED) return;
    if (th->stk_len == 0) return;  /* stack not active, no need to mark. */
    if (th->stk_ptr) {
	rb_gc_mark_locations(th->stk_ptr, th->stk_ptr+th->stk_len);
#if defined(THINK_C) || defined(__human68k__)
	rb_gc_mark_locations(th->stk_ptr+2, th->stk_ptr+th->stk_len+2);
#endif
    }
    frame = th->frame;
    while (frame && frame != top_frame) {
	frame = ADJ(frame);
	rb_gc_mark_frame(frame);
	if (frame->tmp) {
	    struct FRAME *tmp = frame->tmp;

	    while (tmp && tmp != top_frame) {
		tmp = ADJ(tmp);
		rb_gc_mark_frame(tmp);
		tmp = tmp->prev;
	    }
	}
	frame = frame->prev;
    }
    block = th->block;
    while (block) {
	block = ADJ(block);
	rb_gc_mark_frame(&block->frame);
	block = block->prev;
    }
}

void
rb_gc_mark_threads()
{
    rb_thread_t th;

    /* static global mark */
    rb_gc_mark((VALUE)ruby_cref);

    if (!curr_thread) return;
    FOREACH_THREAD(th) {
	rb_gc_mark(th->thread);
    } END_FOREACH(th);
}

static void
thread_free(th)
    rb_thread_t th;
{
    if (th->stk_ptr) free(th->stk_ptr);
    th->stk_ptr = 0;
    if (th->locals) st_free_table(th->locals);
    if (th->status != THREAD_KILLED) {
	if (th->prev) th->prev->next = th->next;
	if (th->next) th->next->prev = th->prev;
    }
    if (th != main_thread) free(th);
}

static rb_thread_t
rb_thread_check(data)
    VALUE data;
{
    if (TYPE(data) != T_DATA || RDATA(data)->dmark != (RUBY_DATA_FUNC)thread_mark) {
	rb_raise(rb_eTypeError, "wrong argument type %s (expected Thread)",
		 rb_obj_classname(data));
    }
    return (rb_thread_t)RDATA(data)->data;
}

static VALUE rb_thread_raise _((int, VALUE*, rb_thread_t));

static int   th_raise_argc;
static VALUE th_raise_argv[2];
static NODE *th_raise_node;
static VALUE th_cmd;
static int   th_sig;
static char *th_signm;

#define RESTORE_NORMAL		1
#define RESTORE_FATAL		2
#define RESTORE_INTERRUPT	3
#define RESTORE_TRAP		4
#define RESTORE_RAISE		5
#define RESTORE_SIGNAL		6

extern VALUE *rb_gc_stack_start;

static void
rb_thread_save_context(th)
    rb_thread_t th;
{
    VALUE *pos;
    int len;
    static VALUE tval;

    len = ruby_stack_length(&pos);
    th->stk_len = 0;
    th->stk_pos = pos;
    if (len > th->stk_max) {
	REALLOC_N(th->stk_ptr, VALUE, len);
	th->stk_max = len;
    }
    th->stk_len = len;
    FLUSH_REGISTER_WINDOWS; 
    MEMCPY(th->stk_ptr, th->stk_pos, VALUE, th->stk_len);
#ifdef SAVE_WIN32_EXCEPTION_LIST
    th->win32_exception_list = win32_get_exception_list();
#endif

    th->frame = ruby_frame;
    th->scope = ruby_scope;
    th->klass = ruby_class;
    th->wrapper = ruby_wrapper;
    th->cref = ruby_cref;
    th->dyna_vars = ruby_dyna_vars;
    th->block = ruby_block;
    th->flags &= THREAD_FLAGS_MASK;
    th->flags |= (rb_trap_immediate<<8) | scope_vmode;
    th->iter = ruby_iter;
    th->tag = prot_tag;
    th->tracing = tracing;
    th->errinfo = ruby_errinfo;
    th->last_status = rb_last_status;
    tval = rb_lastline_get();
    rb_lastline_set(th->last_line);
    th->last_line = tval;
    tval = rb_backref_get();
    rb_backref_set(th->last_match);
    th->last_match = tval;
    th->safe = ruby_safe_level;

    th->node = ruby_current_node;
}

static int
rb_thread_switch(n)
    int n;
{
    rb_trap_immediate = (curr_thread->flags&0x100)?1:0;
    switch (n) {
      case 0:
	return 0;
      case RESTORE_FATAL:
	JUMP_TAG(TAG_FATAL);
	break;
      case RESTORE_INTERRUPT:
	rb_interrupt();
	break;
      case RESTORE_TRAP:
	rb_trap_eval(th_cmd, th_sig);
	break;
      case RESTORE_RAISE:
	ruby_frame->last_func = 0;
	ruby_current_node = th_raise_node;
	rb_f_raise(th_raise_argc, th_raise_argv);
	break;
      case RESTORE_SIGNAL:
	rb_raise(rb_eSignal, "SIG%s", th_signm);
	break;
      case RESTORE_NORMAL:
      default:
	break;
    }
    return 1;
}

#define THREAD_SAVE_CONTEXT(th) \
    (rb_thread_save_context(th),\
     rb_thread_switch((FLUSH_REGISTER_WINDOWS, setjmp((th)->context))))

static void rb_thread_restore_context _((rb_thread_t,int));

static void
stack_extend(th, exit)
    rb_thread_t th;
    int exit;
{
    VALUE space[1024];

    memset(space, 0, 1);	/* prevent array from optimization */
    rb_thread_restore_context(th, exit);
}

static void
rb_thread_restore_context(th, exit)
    rb_thread_t th;
    int exit;
{
    VALUE v;
    static rb_thread_t tmp;
    static int ex;
    static VALUE tval;

    if (!th->stk_ptr) rb_bug("unsaved context");

#if STACK_GROW_DIRECTION < 0
    if (&v > th->stk_pos) stack_extend(th, exit);
#elif STACK_GROW_DIRECTION > 0
    if (&v < th->stk_pos + th->stk_len) stack_extend(th, exit);
#else
    if (&v < rb_gc_stack_start) {
	/* Stack grows downward */
	if (&v > th->stk_pos) stack_extend(th, exit);
    }
    else {
	/* Stack grows upward */
	if (&v < th->stk_pos + th->stk_len) stack_extend(th, exit);
    }
#endif

    rb_trap_immediate = 0;	/* inhibit interrupts from here */
    ruby_frame = th->frame;
    ruby_scope = th->scope;
    ruby_class = th->klass;
    ruby_wrapper = th->wrapper;
    ruby_cref = th->cref;
    ruby_dyna_vars = th->dyna_vars;
    ruby_block = th->block;
    scope_vmode = th->flags&SCOPE_MASK;
    ruby_iter = th->iter;
    prot_tag = th->tag;
    tracing = th->tracing;
    ruby_errinfo = th->errinfo;
    rb_last_status = th->last_status;
    ruby_safe_level = th->safe;

    ruby_current_node = th->node;

#ifdef SAVE_WIN32_EXCEPTION_LIST
    win32_set_exception_list(th->win32_exception_list);
#endif
    tmp = th;
    ex = exit;
    FLUSH_REGISTER_WINDOWS;
    MEMCPY(tmp->stk_pos, tmp->stk_ptr, VALUE, tmp->stk_len);

    tval = rb_lastline_get();
    rb_lastline_set(tmp->last_line);
    tmp->last_line = tval;
    tval = rb_backref_get();
    rb_backref_set(tmp->last_match);
    tmp->last_match = tval;

    longjmp(tmp->context, ex);
}

static void
rb_thread_ready(th)
    rb_thread_t th;
{
    th->wait_for = 0;
    if (th->status != THREAD_TO_KILL) {
	th->status = THREAD_RUNNABLE;
    }
}

static void
rb_thread_die(th)
    rb_thread_t th;
{
    th->thgroup = 0;
    th->status = THREAD_KILLED;
    if (th->stk_ptr) free(th->stk_ptr);
    th->stk_ptr = 0;
}

static void
rb_thread_remove(th)
    rb_thread_t th;
{
    if (th->status == THREAD_KILLED) return;

    rb_thread_ready(th);
    rb_thread_die(th);
    th->prev->next = th->next;
    th->next->prev = th->prev;
}

static int
rb_thread_dead(th)
    rb_thread_t th;
{
    return th->status == THREAD_KILLED;
}

void
rb_thread_fd_close(fd)
    int fd;
{
    rb_thread_t th;

    FOREACH_THREAD(th) {
	if (((th->wait_for & WAIT_FD) && fd == th->fd) ||
	    ((th->wait_for & WAIT_SELECT) && (fd < th->fd) &&
	     (FD_ISSET(fd, &th->readfds) ||
	      FD_ISSET(fd, &th->writefds) ||
	      FD_ISSET(fd, &th->exceptfds)))) {
	    VALUE exc = rb_exc_new2(rb_eIOError, "stream closed");
	    rb_thread_raise(1, &exc, th);
	}
    }
    END_FOREACH(th);
}

static void
rb_thread_deadlock()
{
    char msg[21+SIZEOF_LONG*2];
    VALUE e;

    sprintf(msg, "Thread(0x%lx): deadlock", curr_thread->thread);
    e = rb_exc_new2(rb_eFatal, msg);
    if (curr_thread == main_thread) {
	rb_exc_raise(e);
    }
    curr_thread = main_thread;
    th_raise_argc = 1;
    th_raise_argv[0] = e;
    th_raise_node = ruby_current_node;
    rb_thread_restore_context(main_thread, RESTORE_RAISE);
}

static void
copy_fds(dst, src, max)
    fd_set *dst, *src;
    int max;
{
    int n = 0;
    int i;

    for (i=0; i<=max; i++) {
	if (FD_ISSET(i, src)) {
	    n = i;
	    FD_SET(i, dst);
	}
    }
}

static int
match_fds(dst, src, max)
    fd_set *dst, *src;
    int max;
{
    int i;

    for (i=0; i<=max; i++) {
	if (FD_ISSET(i, src) && FD_ISSET(i, dst)) {
	    return Qtrue;
	}
    }
    return Qfalse;
}

static int
intersect_fds(src, dst, max)
    fd_set *src, *dst;
    int max;
{
    int i, n = 0;

    for (i=0; i<=max; i++) {
	if (FD_ISSET(i, dst)) {
	    if (FD_ISSET(i, src)) {
		/* Wake up only one thread per fd. */
		FD_CLR(i, src);
		++n;
	    }
	    else {
		FD_CLR(i, dst);
	    }
	}
    }
    return n;
}

static int
find_bad_fds(dst, src, max)
    fd_set *dst, *src;
    int max;
{
    int i, test = Qfalse;

    for (i=0; i<=max; i++) {
	if (FD_ISSET(i, src) && !FD_ISSET(i, dst)) {
	    FD_CLR(i, src);
	    test = Qtrue;
	}
    }
    return test;
}

void
rb_thread_schedule()
{
    rb_thread_t next;		/* OK */
    rb_thread_t th;
    rb_thread_t curr;
    int found = 0;

    fd_set readfds;
    fd_set writefds;
    fd_set exceptfds;
    struct timeval delay_tv, *delay_ptr;
    double delay, now;	/* OK */
    int n, max;
    int need_select = 0;
    int select_timeout = 0;

    rb_thread_pending = 0;
    if (curr_thread == curr_thread->next
	&& curr_thread->status == THREAD_RUNNABLE)
	return;

    next = 0;
    curr = curr_thread;		/* starting thread */

    while (curr->status == THREAD_KILLED) {
	curr = curr->prev;
    }

  again:
    max = -1;
    FD_ZERO(&readfds);
    FD_ZERO(&writefds);
    FD_ZERO(&exceptfds);
    delay = DELAY_INFTY;
    now = -1.0;

    FOREACH_THREAD_FROM(curr, th) {
	if (!found && th->status <= THREAD_RUNNABLE) {
	    found = 1;
	}
	if (th->status != THREAD_STOPPED) continue;
	if (th->wait_for & WAIT_JOIN) {
	    if (rb_thread_dead(th->join)) {
		th->status = THREAD_RUNNABLE;
		found = 1;
	    }
	}
	if (th->wait_for & WAIT_FD) {
	    FD_SET(th->fd, &readfds);
	    if (max < th->fd) max = th->fd;
	    need_select = 1;
	}
	if (th->wait_for & WAIT_SELECT) {
	    copy_fds(&readfds, &th->readfds, th->fd);
	    copy_fds(&writefds, &th->writefds, th->fd);
	    copy_fds(&exceptfds, &th->exceptfds, th->fd);
	    if (max < th->fd) max = th->fd;
	    need_select = 1;
	    if (th->wait_for & WAIT_TIME) {
		select_timeout = 1;
	    }
	    th->select_value = 0;
	}
	if (th->wait_for & WAIT_TIME) {
	    double th_delay;

	    if (now < 0.0) now = timeofday();
	    th_delay = th->delay - now;
	    if (th_delay <= 0.0) {
		th->status = THREAD_RUNNABLE;
		found = 1;
	    }
	    else if (th_delay < delay) {
		delay = th_delay;
		need_select = 1;
	    }
	    else if (th->delay == DELAY_INFTY) {
		need_select = 1;
	    }
	}
    }
    END_FOREACH_FROM(curr, th);
    
    /* Do the select if needed */
    if (need_select) {
	/* Convert delay to a timeval */
	/* If a thread is runnable, just poll */
	if (found) {
	    delay_tv.tv_sec = 0;
	    delay_tv.tv_usec = 0;
	    delay_ptr = &delay_tv;
	}
	else if (delay == DELAY_INFTY) {
	    delay_ptr = 0;
	}
	else {
	    delay_tv.tv_sec = delay;
	    delay_tv.tv_usec = (delay - (double)delay_tv.tv_sec)*1e6;
	    delay_ptr = &delay_tv;
	}

	n = select(max+1, &readfds, &writefds, &exceptfds, delay_ptr);
	if (n < 0) {
	    int e = errno;

	    if (rb_trap_pending) rb_trap_exec();
	    if (e == EINTR) goto again;
#ifdef ERESTART
	    if (e == ERESTART) goto again;
#endif
	    FOREACH_THREAD_FROM(curr, th) {
		if (th->wait_for & WAIT_SELECT) {
		    int v = 0;

		    v |= find_bad_fds(&readfds, &th->readfds, th->fd);
		    v |= find_bad_fds(&writefds, &th->writefds, th->fd);
		    v |= find_bad_fds(&exceptfds, &th->exceptfds, th->fd);
		    if (v) {
			th->select_value = n;
			n = max;
		    }
		}
	    }
	    END_FOREACH_FROM(curr, th);
	}
 	if (select_timeout && n == 0) {
 	    if (now < 0.0) now = timeofday();
 	    FOREACH_THREAD_FROM(curr, th) {
 		if (((th->wait_for&(WAIT_SELECT|WAIT_TIME)) == (WAIT_SELECT|WAIT_TIME)) &&
		    th->delay <= now) {
 		    th->status = THREAD_RUNNABLE;
 		    th->wait_for = 0;
 		    th->select_value = 0;
 		    found = 1;
                    intersect_fds(&readfds, &th->readfds, max);
                    intersect_fds(&writefds, &th->writefds, max);
                    intersect_fds(&exceptfds, &th->exceptfds, max);
		}
	    }
	    END_FOREACH_FROM(curr, th);
	}
	if (n > 0) {
	    now = -1.0;
	    /* Some descriptors are ready. 
	       Make the corresponding threads runnable. */
	    FOREACH_THREAD_FROM(curr, th) {
		if ((th->wait_for&WAIT_FD) && FD_ISSET(th->fd, &readfds)) {
		    /* Wake up only one thread per fd. */
		    FD_CLR(th->fd, &readfds);
		    th->status = THREAD_RUNNABLE;
		    th->fd = 0;
		    th->wait_for = 0;
		    found = 1;
		}
		if ((th->wait_for&WAIT_SELECT) &&
		    (match_fds(&readfds, &th->readfds, max) ||
		     match_fds(&writefds, &th->writefds, max) ||
		     match_fds(&exceptfds, &th->exceptfds, max))) {
		    /* Wake up only one thread per fd. */
		    th->status = THREAD_RUNNABLE;
		    th->wait_for = 0;
		    n = intersect_fds(&readfds, &th->readfds, max) +
			intersect_fds(&writefds, &th->writefds, max) +
			intersect_fds(&exceptfds, &th->exceptfds, max);
		    th->select_value = n;
		    found = 1;
		}
	    }
	    END_FOREACH_FROM(curr, th);
	}
	/* The delays for some of the threads should have expired.
	   Go through the loop once more, to check the delays. */
	if (!found && delay != DELAY_INFTY)
	    goto again;
    }

    FOREACH_THREAD_FROM(curr, th) {
	if (th->status == THREAD_TO_KILL) {
	    next = th;
	    break;
	}
	if (th->status == THREAD_RUNNABLE && th->stk_ptr) {
	    if (!next || next->priority < th->priority) 
	       next = th;
	}
    }
    END_FOREACH_FROM(curr, th); 

    if (!next) {
	/* raise fatal error to main thread */
	curr_thread->node = ruby_current_node;
	if (curr->next == curr) {
	    TRAP_BEG;
	    pause();
	    TRAP_END;
	}
	FOREACH_THREAD_FROM(curr, th) {
	    warn_printf("deadlock 0x%lx: %s:",
			th->thread, thread_status_name(th->status));
	    if (th->wait_for & WAIT_FD) warn_printf("F(%d)", th->fd);
	    if (th->wait_for & WAIT_SELECT) warn_printf("S");
	    if (th->wait_for & WAIT_TIME) warn_printf("T(%f)", th->delay);
	    if (th->wait_for & WAIT_JOIN)
		warn_printf("J(0x%lx)", th->join ? th->join->thread : 0);
	    if (th->wait_for & WAIT_PID) warn_printf("P");
	    if (!th->wait_for) warn_printf("-");
	    warn_printf(" %s - %s:%d\n",
			th==main_thread ? "(main)" : "",
			th->node->nd_file, nd_line(th->node));
	}
	END_FOREACH_FROM(curr, th);
	next = main_thread;
	rb_thread_ready(next);
	next->status = THREAD_TO_KILL;
	if (!rb_thread_dead(curr_thread)) {
	    rb_thread_save_context(curr_thread);
	}
	rb_thread_deadlock();
    }
    next->wait_for = 0;
    if (next->status == THREAD_RUNNABLE && next == curr_thread) {
	return;
    }

    /* context switch */
    if (curr == curr_thread) {
	if (THREAD_SAVE_CONTEXT(curr)) {
	    return;
	}
    }

    curr_thread = next;
    if (next->status == THREAD_TO_KILL) {
	if (!(next->flags & THREAD_TERMINATING)) {
	    next->flags |= THREAD_TERMINATING;
	    /* terminate; execute ensure-clause if any */
	    rb_thread_restore_context(next, RESTORE_FATAL);
	}
    }
    rb_thread_restore_context(next, RESTORE_NORMAL);
}

void
rb_thread_wait_fd(fd)
    int fd;
{
    if (rb_thread_critical) return;
    if (ruby_in_compile) return;
    if (curr_thread == curr_thread->next) return;
    if (curr_thread->status == THREAD_TO_KILL) return;

    curr_thread->status = THREAD_STOPPED;
    curr_thread->fd = fd;
    curr_thread->wait_for = WAIT_FD;
    rb_thread_schedule();
}

int
rb_thread_fd_writable(fd)
    int fd;
{
    if (rb_thread_critical) return Qtrue;
    if (curr_thread == curr_thread->next) return Qtrue;
    if (curr_thread->status == THREAD_TO_KILL) return Qtrue;

    curr_thread->status = THREAD_STOPPED;
    FD_ZERO(&curr_thread->readfds);
    FD_ZERO(&curr_thread->writefds);
    FD_SET(fd, &curr_thread->writefds);
    FD_ZERO(&curr_thread->exceptfds);
    curr_thread->fd = fd+1;
    curr_thread->wait_for = WAIT_SELECT;
    rb_thread_schedule();
    return Qfalse;
}

void
rb_thread_wait_for(time)
    struct timeval time;
{
    double date;

    if (rb_thread_critical ||
	curr_thread == curr_thread->next ||
	curr_thread->status == THREAD_TO_KILL) {
	int n;
#ifndef linux
	double d, limit;
	limit = timeofday()+(double)time.tv_sec+(double)time.tv_usec*1e-6;
#endif
	for (;;) {
	    TRAP_BEG;
	    n = select(0, 0, 0, 0, &time);
	    TRAP_END;
	    if (n == 0) return;
	    if (n < 0) {
		switch (errno) {
		  case EINTR:
#ifdef ERESTART
		  case ERESTART:
#endif
		    return;
		  default:
		    rb_sys_fail("sleep");
		}
	    }
#ifndef linux
	    d = limit - timeofday();

	    time.tv_sec = (int)d;
	    time.tv_usec = (int)((d - (int)d)*1e6);
	    if (time.tv_usec < 0) {
		time.tv_usec += (long)1e6;
		time.tv_sec -= 1;
	    }
	    if (time.tv_sec < 0) return;
#endif
	}
    }

    date = timeofday() + (double)time.tv_sec + (double)time.tv_usec*1e-6;
    curr_thread->status = THREAD_STOPPED;
    curr_thread->delay = date;
    curr_thread->wait_for = WAIT_TIME;
    rb_thread_schedule();
}

void rb_thread_sleep_forever _((void));

int
rb_thread_alone()
{
    return curr_thread == curr_thread->next;
}

int
rb_thread_select(max, read, write, except, timeout)
    int max;
    fd_set *read, *write, *except;
    struct timeval *timeout;
{
    double limit;
    int n;

    if (!read && !write && !except) {
	if (!timeout) {
	    rb_thread_sleep_forever();
	    return 0;
	}
	rb_thread_wait_for(*timeout);
	return 0;
    }

    if (timeout) {
	limit = timeofday()+
	    (double)timeout->tv_sec+(double)timeout->tv_usec*1e-6;
    }

    if (rb_thread_critical ||
	curr_thread == curr_thread->next ||
	curr_thread->status == THREAD_TO_KILL) {
#ifndef linux
	struct timeval tv, *tvp = timeout;

	if (timeout) {
	    tv = *timeout;
	    tvp = &tv;
	}
#else
	struct timeval *const tvp = timeout;
#endif
	for (;;) {
	    TRAP_BEG;
	    n = select(max, read, write, except, tvp);
	    TRAP_END;
	    if (n < 0) {
		switch (errno) {
		  case EINTR:
#ifdef ERESTART
		  case ERESTART:
#endif
#ifndef linux
		    if (timeout) {
			double d = limit - timeofday();

			tv.tv_sec = (unsigned int)d;
			tv.tv_usec = (long)((d-(double)tv.tv_sec)*1e6);
			if (tv.tv_sec < 0)  tv.tv_sec = 0;
			if (tv.tv_usec < 0) tv.tv_usec = 0;
		    }
#endif
		    continue;
		  default:
		    break;
		}
	    }
	    return n;
	}
    }

    curr_thread->status = THREAD_STOPPED;
    if (read) curr_thread->readfds = *read;
    else FD_ZERO(&curr_thread->readfds);
    if (write) curr_thread->writefds = *write;
    else FD_ZERO(&curr_thread->writefds);
    if (except) curr_thread->exceptfds = *except;
    else FD_ZERO(&curr_thread->exceptfds);
    curr_thread->fd = max;
    curr_thread->wait_for = WAIT_SELECT;
    if (timeout) {
	curr_thread->delay = timeofday() +
	    (double)timeout->tv_sec + (double)timeout->tv_usec*1e-6;
	curr_thread->wait_for |= WAIT_TIME;
    }
    rb_thread_schedule();
    if (read) *read = curr_thread->readfds;
    if (write) *write = curr_thread->writefds;
    if (except) *except = curr_thread->exceptfds;
    return curr_thread->select_value;
}

static int rb_thread_join _((rb_thread_t, double));

static int
rb_thread_join(th, limit)
    rb_thread_t th;
    double limit;
{
    enum thread_status last_status = THREAD_RUNNABLE;

    if (rb_thread_critical) rb_thread_deadlock();
    if (!rb_thread_dead(th)) {
	if (th == curr_thread)
	    rb_raise(rb_eThreadError, "thread 0x%lx tried to join itself",
		     th->thread);
	if ((th->wait_for & WAIT_JOIN) && th->join == curr_thread)
	    rb_raise(rb_eThreadError, "Thread#join: deadlock 0x%lx - mutual join(0x%lx)",
		     curr_thread->thread, th->thread);
	if (curr_thread->status == THREAD_TO_KILL)
	    last_status = THREAD_TO_KILL;
	if (limit == 0) return Qfalse;
	curr_thread->status = THREAD_STOPPED;
	curr_thread->join = th;
	curr_thread->wait_for = WAIT_JOIN;
	curr_thread->delay = timeofday() + limit;
	if (limit < DELAY_INFTY) curr_thread->wait_for |= WAIT_TIME;
	rb_thread_schedule();
	curr_thread->status = last_status;
	if (!rb_thread_dead(th)) return Qfalse;
    }

    if (!NIL_P(th->errinfo) && (th->flags & THREAD_RAISED)) {
	VALUE oldbt = get_backtrace(th->errinfo);
	VALUE errat = make_backtrace();

	if (TYPE(oldbt) == T_ARRAY && RARRAY(oldbt)->len > 0) {
	    rb_ary_unshift(errat, rb_ary_entry(oldbt, 0));
	}
	set_backtrace(th->errinfo, errat);
	rb_exc_raise(th->errinfo);
    }

    return Qtrue;
}

static VALUE
rb_thread_join_m(argc, argv, thread)
    int argc;
    VALUE *argv;
    VALUE thread;
{
    VALUE limit;
    double delay = DELAY_INFTY;
    rb_thread_t th = rb_thread_check(thread);

    rb_scan_args(argc, argv, "01", &limit);
    if (!NIL_P(limit)) delay = rb_num2dbl(limit);
    if (!rb_thread_join(th, delay))
	return Qnil;
    return thread;
}

VALUE
rb_thread_current()
{
    return curr_thread->thread;
}

VALUE
rb_thread_main()
{
    return main_thread->thread;
}

VALUE
rb_thread_list()
{
    rb_thread_t th;
    VALUE ary = rb_ary_new();

    FOREACH_THREAD(th) {
	switch (th->status) {
	  case THREAD_RUNNABLE:
	  case THREAD_STOPPED:
	  case THREAD_TO_KILL:
	    rb_ary_push(ary, th->thread);
	  default:
	    break;
	}
    }
    END_FOREACH(th);

    return ary;
}

VALUE
rb_thread_wakeup(thread)
    VALUE thread;
{
    rb_thread_t th = rb_thread_check(thread);

    if (th->status == THREAD_KILLED)
	rb_raise(rb_eThreadError, "killed thread");
    rb_thread_ready(th);

    return thread;
}

VALUE
rb_thread_run(thread)
    VALUE thread;
{
    rb_thread_wakeup(thread);
    if (!rb_thread_critical) rb_thread_schedule();

    return thread;
}

VALUE
rb_thread_kill(thread)
    VALUE thread;
{
    rb_thread_t th = rb_thread_check(thread);

    if (th != curr_thread && th->safe < 4) {
	rb_secure(4);
    }
    if (th->status == THREAD_TO_KILL || th->status == THREAD_KILLED)
	return thread; 
    if (th == th->next || th == main_thread) rb_exit(0);

    rb_thread_ready(th);
    th->status = THREAD_TO_KILL;
    if (!rb_thread_critical) rb_thread_schedule();
    return thread;
}

static VALUE
rb_thread_s_kill(obj, th)
    VALUE obj, th;
{
    return rb_thread_kill(th);
}

static VALUE
rb_thread_exit()
{
    return rb_thread_kill(curr_thread->thread);
}

static VALUE
rb_thread_pass()
{
    rb_thread_schedule();
    return Qnil;
}

VALUE
rb_thread_stop()
{
    enum thread_status last_status = THREAD_RUNNABLE;

    rb_thread_critical = 0;
    if (curr_thread == curr_thread->next) {
	rb_raise(rb_eThreadError, "stopping only thread\n\tnote: use sleep to stop forever");
    }
    if (curr_thread->status == THREAD_TO_KILL)
	last_status = THREAD_TO_KILL;
    curr_thread->status = THREAD_STOPPED;
    rb_thread_schedule();
    curr_thread->status = last_status;

    return Qnil;
}

struct timeval rb_time_timeval();

void
rb_thread_polling()
{
    if (curr_thread != curr_thread->next) {
	curr_thread->status = THREAD_STOPPED;
	curr_thread->delay = timeofday() + (double)0.06;
	curr_thread->wait_for = WAIT_TIME;
	rb_thread_schedule();
    }
}

void
rb_thread_sleep(sec)
    int sec;
{
    if (curr_thread == curr_thread->next) {
	TRAP_BEG;
	sleep(sec);
	TRAP_END;
	return;
    }
    rb_thread_wait_for(rb_time_timeval(INT2FIX(sec)));
}

void
rb_thread_sleep_forever()
{
    if (curr_thread == curr_thread->next ||
	curr_thread->status == THREAD_TO_KILL) {
	TRAP_BEG;
	pause();
	TRAP_END;
	return;
    }

    curr_thread->delay = DELAY_INFTY;
    curr_thread->wait_for = WAIT_TIME;
    curr_thread->status = THREAD_STOPPED;
    rb_thread_schedule();
}

static VALUE
rb_thread_priority(thread)
    VALUE thread;
{
    return INT2NUM(rb_thread_check(thread)->priority);
}

static VALUE
rb_thread_priority_set(thread, prio)
    VALUE thread, prio;
{
    rb_thread_t th;

    rb_secure(4);
    th = rb_thread_check(thread);

    th->priority = NUM2INT(prio);
    rb_thread_schedule();
    return prio;
}

static VALUE
rb_thread_safe_level(thread)
    VALUE thread;
{
    rb_thread_t th;

    th = rb_thread_check(thread);
    if (th == curr_thread) {
	return INT2NUM(ruby_safe_level);
    }
    return INT2NUM(th->safe);
}

static int ruby_thread_abort;
static VALUE thgroup_default;

static VALUE
rb_thread_s_abort_exc()
{
    return ruby_thread_abort?Qtrue:Qfalse;
}

static VALUE
rb_thread_s_abort_exc_set(self, val)
    VALUE self, val;
{
    rb_secure(4);
    ruby_thread_abort = RTEST(val);
    return val;
}

static VALUE
rb_thread_abort_exc(thread)
    VALUE thread;
{
    return rb_thread_check(thread)->abort?Qtrue:Qfalse;
}

static VALUE
rb_thread_abort_exc_set(thread, val)
    VALUE thread, val;
{
    rb_secure(4);
    rb_thread_check(thread)->abort = RTEST(val);
    return val;
}

VALUE
rb_thread_group(thread)
    VALUE thread;
{
    VALUE group = rb_thread_check(thread)->thgroup;
    if (!group) {
	group = Qnil;
    }
    return group;
}

#define THREAD_ALLOC(th) do {\
    th = ALLOC(struct thread);\
\
    th->next = 0;\
    th->prev = 0;\
\
    th->status = THREAD_RUNNABLE;\
    th->result = 0;\
    th->flags = 0;\
\
    th->stk_ptr = 0;\
    th->stk_len = 0;\
    th->stk_max = 0;\
    th->wait_for = 0;\
    FD_ZERO(&th->readfds);\
    FD_ZERO(&th->writefds);\
    FD_ZERO(&th->exceptfds);\
    th->delay = 0.0;\
    th->join = 0;\
\
    th->frame = 0;\
    th->scope = 0;\
    th->klass = 0;\
    th->wrapper = 0;\
    th->cref = ruby_cref;\
    th->dyna_vars = ruby_dyna_vars;\
    th->block = 0;\
    th->iter = 0;\
    th->tag = 0;\
    th->tracing = 0;\
    th->errinfo = Qnil;\
    th->last_status = 0;\
    th->last_line = 0;\
    th->last_match = Qnil;\
    th->abort = 0;\
    th->priority = 0;\
    th->thgroup = thgroup_default;\
    th->locals = 0;\
} while (0)

static rb_thread_t
rb_thread_alloc(klass)
    VALUE klass;
{
    rb_thread_t th;
    struct RVarmap *vars;

    THREAD_ALLOC(th);
    th->thread = Data_Wrap_Struct(klass, thread_mark, thread_free, th);

    for (vars = th->dyna_vars; vars; vars = vars->next) {
	if (FL_TEST(vars, DVAR_DONT_RECYCLE)) break;
	FL_SET(vars, DVAR_DONT_RECYCLE);
    }
    return th;
}

#if defined(HAVE_SETITIMER)
static void
catch_timer(sig)
    int sig;
{
#if !defined(POSIX_SIGNAL) && !defined(BSD_SIGNAL)
    signal(sig, catch_timer);
#endif
    if (!rb_thread_critical) {
	if (rb_trap_immediate) {
	    rb_thread_schedule();
	}
	else rb_thread_pending = 1;
    }
}
#else
int rb_thread_tick = THREAD_TICK;
#endif

#if defined(HAVE_SETITIMER)
static int thread_init = 0;

void
rb_thread_start_timer()
{
    struct itimerval tval;

    if (!thread_init) return;
    tval.it_interval.tv_sec = 0;
    tval.it_interval.tv_usec = 10000;
    tval.it_value = tval.it_interval;
    setitimer(ITIMER_VIRTUAL, &tval, NULL);
}

void
rb_thread_stop_timer()
{
    struct itimerval tval;

    if (!thread_init) return;
    tval.it_interval.tv_sec = 0;
    tval.it_interval.tv_usec = 0;
    tval.it_value = tval.it_interval;
    setitimer(ITIMER_VIRTUAL, &tval, NULL);
}
#endif

static VALUE
rb_thread_start_0(fn, arg, th_arg)
    VALUE (*fn)();
    void *arg;
    rb_thread_t th_arg;
{
    volatile rb_thread_t th = th_arg;
    volatile VALUE thread = th->thread;
    struct BLOCK* saved_block = 0;
    enum thread_status status;
    int state;

    if (OBJ_FROZEN(curr_thread->thgroup)) {
	rb_raise(rb_eThreadError, 
		 "can't start a new thread (frozen ThreadGroup)");
    }

#if defined(HAVE_SETITIMER)
    if (!thread_init) {
#ifdef POSIX_SIGNAL
	posix_signal(SIGVTALRM, catch_timer);
#else
	signal(SIGVTALRM, catch_timer);
#endif

	thread_init = 1;
	rb_thread_start_timer();
    }
#endif

    if (THREAD_SAVE_CONTEXT(curr_thread)) {
	return thread;
    }

    if (ruby_block) {		/* should nail down higher blocks */
	struct BLOCK dummy;

	dummy.prev = ruby_block;
	blk_copy_prev(&dummy);
	saved_block = ruby_block = dummy.prev;
    }
    scope_dup(ruby_scope);

    if (!th->next) {
	/* merge in thread list */
	th->prev = curr_thread;
	curr_thread->next->prev = th;
	th->next = curr_thread->next;
	curr_thread->next = th;
	th->priority = curr_thread->priority;
	th->thgroup = curr_thread->thgroup;
    }

    PUSH_TAG(PROT_THREAD);
    if ((state = EXEC_TAG()) == 0) {
	if (THREAD_SAVE_CONTEXT(th) == 0) {
	    curr_thread = th;
	    th->result = (*fn)(arg, th);
	}
    }
    POP_TAG();
    status = th->status;

    if (th == main_thread) ruby_stop(state);
    rb_thread_remove(th);

    while (saved_block) {
	struct BLOCK *tmp = saved_block;

	if (tmp->frame.argc > 0)
	    free(tmp->frame.argv);
	saved_block = tmp->prev;
	free(tmp);
    }

    if (state && status != THREAD_TO_KILL && !NIL_P(ruby_errinfo)) {
	th->flags |= THREAD_RAISED;
	if (state == TAG_FATAL) { 
	    /* fatal error within this thread, need to stop whole script */
	    main_thread->errinfo = ruby_errinfo;
	    rb_thread_cleanup();
	}
	else if (rb_obj_is_kind_of(ruby_errinfo, rb_eSystemExit)) {
	    if (th->safe >= 4) {
		char buf[32];

		sprintf(buf, "Insecure exit at level %d", th->safe);
		th->errinfo = rb_exc_new2(rb_eSecurityError, buf);
	    }
	    else {
		/* delegate exception to main_thread */
		rb_thread_raise(1, &ruby_errinfo, main_thread);
	    }
	}
	else if (th->safe < 4 && (ruby_thread_abort || th->abort || RTEST(ruby_debug))) {
	    VALUE err = system_exit(1, 0, 0);
	    error_print();
	    /* exit on main_thread */
	    rb_thread_raise(1, &err, main_thread);
	}
	else {
	    th->errinfo = ruby_errinfo;
	}
    }
    rb_thread_schedule();
    ruby_stop(0);		/* last thread termination */
    return 0;			/* not reached */
}

VALUE
rb_thread_create(fn, arg)
    VALUE (*fn)();
    void *arg;
{
    Init_stack((VALUE*)&arg);
    return rb_thread_start_0(fn, arg, rb_thread_alloc(rb_cThread));
}

static VALUE
rb_thread_yield(arg, th) 
    VALUE arg;
    rb_thread_t th;
{
    const ID *tbl;

    scope_dup(ruby_block->scope);

    tbl = ruby_scope->local_tbl;
    if (tbl) {
	int n = *tbl++;
	for (tbl += 2, n -= 2; n > 0; --n) { /* skip first 2 ($_ and $~) */
	    ID id = *tbl++;
	    if (id != 0 && !rb_is_local_id(id))  /* push flip states */
		rb_dvar_push(id, Qfalse);
	}
    }
    rb_dvar_push('_', Qnil);
    rb_dvar_push('~', Qnil);
    ruby_block->dyna_vars = ruby_dyna_vars;

    return rb_yield_0(arg, 0, 0, Qtrue, Qtrue);
}

static VALUE
rb_thread_s_new(argc, argv, klass)
    int argc;
    VALUE *argv;
    VALUE klass;
{
    rb_thread_t th = rb_thread_alloc(klass);
    volatile VALUE *pos;

    pos = th->stk_pos;
    rb_obj_call_init(th->thread, argc, argv);
    if (th->stk_pos == 0) {
	rb_raise(rb_eThreadError, "uninitialized thread - check `%s#initialize'",
		 rb_class2name(klass));
    }

    return th->thread;
}

static VALUE
rb_thread_initialize(thread, args)
    VALUE thread, args;
{
    if (!rb_block_given_p()) {
	rb_raise(rb_eThreadError, "must be called with a block");
    }
    return rb_thread_start_0(rb_thread_yield, args, rb_thread_check(thread));
}

static VALUE
rb_thread_start(klass, args)
    VALUE klass, args;
{
    if (!rb_block_given_p()) {
	rb_raise(rb_eThreadError, "must be called with a block");
    }
    return rb_thread_start_0(rb_thread_yield, args, rb_thread_alloc(klass));
}

static VALUE
rb_thread_value(thread)
    VALUE thread;
{
    rb_thread_t th = rb_thread_check(thread);

    while (!rb_thread_join(th, DELAY_INFTY));

    return th->result;
}

static VALUE
rb_thread_status(thread)
    VALUE thread;
{
    rb_thread_t th = rb_thread_check(thread);

    if (rb_thread_dead(th)) {
	if (!NIL_P(th->errinfo) && (th->flags & THREAD_RAISED))
	    return Qnil;
	return Qfalse;
    }

    return rb_str_new2(thread_status_name(th->status));
}

static VALUE
rb_thread_alive_p(thread)
    VALUE thread;
{
    rb_thread_t th = rb_thread_check(thread);

    if (rb_thread_dead(th)) return Qfalse;
    return Qtrue;
}

static VALUE
rb_thread_stop_p(thread)
    VALUE thread;
{
    rb_thread_t th = rb_thread_check(thread);

    if (rb_thread_dead(th)) return Qtrue;
    if (th->status == THREAD_STOPPED) return Qtrue;
    return Qfalse;
}

static void
rb_thread_wait_other_threads()
{
    rb_thread_t th;
    int found;

    /* wait other threads to terminate */
    while (curr_thread != curr_thread->next) {
	found = 0;
	FOREACH_THREAD(th) {
	    if (th != curr_thread && th->status != THREAD_STOPPED) {
		found = 1;
		break;
	    }
	}
	END_FOREACH(th);
	if (!found) return;
	rb_thread_schedule();
    }
}

static void
rb_thread_cleanup()
{
    rb_thread_t curr, th;

    curr = curr_thread;
    while (curr->status == THREAD_KILLED) {
	curr = curr->prev;
    }

    FOREACH_THREAD_FROM(curr, th) {
	if (th->status != THREAD_KILLED) {
	    rb_thread_ready(th);
	    th->thgroup = 0;
	    th->priority = 0;
	    if (th != main_thread) {
		th->status = THREAD_TO_KILL;
		RDATA(th->thread)->dfree = NULL;
	    }
	}
    }
    END_FOREACH_FROM(curr, th);
}

int rb_thread_critical;

static VALUE
rb_thread_critical_get()
{
    return rb_thread_critical?Qtrue:Qfalse;
}

static VALUE
rb_thread_critical_set(obj, val)
    VALUE obj, val;
{
    rb_thread_critical = RTEST(val);
    return val;
}

void
rb_thread_interrupt()
{
    rb_thread_critical = 0;
    rb_thread_ready(main_thread);
    if (curr_thread == main_thread) {
	rb_interrupt();
    }
    if (!rb_thread_dead(curr_thread)) {
	if (THREAD_SAVE_CONTEXT(curr_thread)) {
	    return;
	}
    }
    curr_thread = main_thread;
    rb_thread_restore_context(curr_thread, RESTORE_INTERRUPT);
}

void
rb_thread_signal_raise(sig)
    char *sig;
{
    if (sig == 0) return;	/* should not happen */
    rb_thread_critical = 0;
    if (curr_thread == main_thread) {
	rb_thread_ready(curr_thread);
	rb_raise(rb_eSignal, "SIG%s", sig);
    }
    rb_thread_ready(main_thread);
    if (!rb_thread_dead(curr_thread)) {
	if (THREAD_SAVE_CONTEXT(curr_thread)) {
	    return;
	}
    }
    th_signm = sig;
    curr_thread = main_thread;
    rb_thread_restore_context(curr_thread, RESTORE_SIGNAL);
}

void
rb_thread_trap_eval(cmd, sig)
    VALUE cmd;
    int sig;
{
#if 0
    rb_thread_critical = 0;
    if (!rb_thread_dead(curr_thread)) {
	rb_thread_ready(curr_thread);
	rb_trap_eval(cmd, sig);
	return;
    }
    rb_thread_ready(main_thread);
    if (THREAD_SAVE_CONTEXT(curr_thread)) {
	return;
    }
    th_cmd = cmd;
    th_sig = sig;
    curr_thread = main_thread;
    rb_thread_restore_context(curr_thread, RESTORE_TRAP);
#else
    rb_thread_critical = 0;
    if (!rb_thread_dead(curr_thread)) {
	if (THREAD_SAVE_CONTEXT(curr_thread)) {
	    return;
	}
    }
    th_cmd = cmd;
    th_sig = sig;
    curr_thread = main_thread;
    rb_thread_restore_context(curr_thread, RESTORE_TRAP);
#endif
}

static VALUE
rb_thread_raise(argc, argv, th)
    int argc;
    VALUE *argv;
    rb_thread_t th;
{
    if (rb_thread_dead(th)) return Qnil;
    if (curr_thread == th) {
	rb_f_raise(argc, argv);
    }

    if (!rb_thread_dead(curr_thread)) {
	if (THREAD_SAVE_CONTEXT(curr_thread)) {
	    return th->thread;
	}
    }

    rb_scan_args(argc, argv, "11", &th_raise_argv[0], &th_raise_argv[1]);
    rb_thread_ready(th);
    curr_thread = th;

    th_raise_argc = argc;
    th_raise_node = ruby_current_node;
    rb_thread_restore_context(curr_thread, RESTORE_RAISE);
    return Qnil;		/* not reached */
}

static VALUE
rb_thread_raise_m(argc, argv, thread)
    int argc;
    VALUE *argv;
    VALUE thread;
{
    rb_thread_t th = rb_thread_check(thread);

    if (ruby_safe_level > th->safe) {
	rb_secure(4);
    }
    rb_thread_raise(argc, argv, th);
    return Qnil;		/* not reached */
}

VALUE
rb_thread_local_aref(thread, id)
    VALUE thread;
    ID id;
{
    rb_thread_t th;
    VALUE val;

    th = rb_thread_check(thread);
    if (ruby_safe_level >= 4 && th != curr_thread) {
	rb_raise(rb_eSecurityError, "Insecure: thread locals");
    }
    if (!th->locals) return Qnil;
    if (st_lookup(th->locals, id, &val)) {
	return val;
    }
    return Qnil;
}

static VALUE
rb_thread_aref(thread, id)
    VALUE thread, id;
{
    return rb_thread_local_aref(thread, rb_to_id(id));
}

VALUE
rb_thread_local_aset(thread, id, val)
    VALUE thread;
    ID id;
    VALUE val;
{
    rb_thread_t th = rb_thread_check(thread);

    if (ruby_safe_level >= 4 && th != curr_thread) {
	rb_raise(rb_eSecurityError, "Insecure: can't modify thread locals");
    }
    if (OBJ_FROZEN(thread)) rb_error_frozen("thread locals");

    if (!th->locals) {
	th->locals = st_init_numtable();
    }
    if (NIL_P(val)) {
	st_delete(th->locals, (st_data_t*)&id, 0);
	return Qnil;
    }
    st_insert(th->locals, id, val);

    return val;
}

static VALUE
rb_thread_aset(thread, id, val)
    VALUE thread, id, val;
{
    return rb_thread_local_aset(thread, rb_to_id(id), val);
}

static VALUE
rb_thread_key_p(thread, id)
    VALUE thread, id;
{
    rb_thread_t th = rb_thread_check(thread);

    if (!th->locals) return Qfalse;
    if (st_lookup(th->locals, rb_to_id(id), 0))
	return Qtrue;
    return Qfalse;
}

static int
thread_keys_i(key, value, ary)
    ID key;
    VALUE value, ary;
{
    rb_ary_push(ary, ID2SYM(key));
    return ST_CONTINUE;
}

static VALUE
rb_thread_keys(thread)
    VALUE thread;
{
    rb_thread_t th = rb_thread_check(thread);
    VALUE ary = rb_ary_new();

    if (th->locals) {
	st_foreach(th->locals, thread_keys_i, ary);
    }
    return ary;
}

static VALUE
rb_thread_inspect(thread)
    VALUE thread;
{
    char *cname = rb_obj_classname(thread);
    rb_thread_t th = rb_thread_check(thread);
    const char *status = thread_status_name(th->status);
    VALUE str;

    str = rb_str_new(0, strlen(cname)+7+16+9+1); /* 7:tags 16:addr 9:status 1:nul */ 
    sprintf(RSTRING(str)->ptr, "#<%s:0x%lx %s>", cname, thread, status);
    RSTRING(str)->len = strlen(RSTRING(str)->ptr);
    OBJ_INFECT(str, thread);

    return str;
}

void
rb_thread_atfork()
{
    rb_thread_t th;

    if (rb_thread_alone()) return;
    FOREACH_THREAD(th) {
	if (th != curr_thread) {
	    rb_thread_die(th);
	}
    }
    END_FOREACH(th);
    main_thread = curr_thread;
    curr_thread->next = curr_thread;
    curr_thread->prev = curr_thread;
}

static VALUE rb_cCont;

static VALUE
rb_callcc(self)
    VALUE self;
{
    volatile VALUE cont;
    rb_thread_t th;
    struct tag *tag;
    struct RVarmap *vars;

    THREAD_ALLOC(th);
    cont = Data_Wrap_Struct(rb_cCont, thread_mark, thread_free, th);

    scope_dup(ruby_scope);
    for (tag=prot_tag; tag; tag=tag->prev) {
	scope_dup(tag->scope);
    }
    if (ruby_block) {
	struct BLOCK *block = ruby_block;

	while (block) {
	    block->tag->flags |= BLOCK_DYNAMIC;
	    block = block->prev;
	}
    }
    th->thread = curr_thread->thread;

    for (vars = th->dyna_vars; vars; vars = vars->next) {
	if (FL_TEST(vars, DVAR_DONT_RECYCLE)) break;
	FL_SET(vars, DVAR_DONT_RECYCLE);
    }

    if (THREAD_SAVE_CONTEXT(th)) {
	return th->result;
    }
    else {
	return rb_yield(cont);
    }
}

static VALUE
rb_cont_call(argc, argv, cont)
    int argc;
    VALUE *argv;
    VALUE cont;
{
    rb_thread_t th = rb_thread_check(cont);

    if (th->thread != curr_thread->thread) {
	rb_raise(rb_eRuntimeError, "continuation called across threads");
    }
    switch (argc) {
      case 0:
	th->result = Qnil;
	break;
      case 1:
	th->result = argv[0];
	break;
      default:
	th->result = rb_ary_new4(argc, argv);
	break;
    }

    rb_thread_restore_context(th, RESTORE_NORMAL);
    return Qnil;
}

struct thgroup {
    int enclosed;
    VALUE group;
};

static VALUE thgroup_s_alloc _((VALUE));
static VALUE
thgroup_s_alloc(klass)
    VALUE klass;
{
    VALUE group;
    struct thgroup *data;

    group = Data_Make_Struct(klass, struct thgroup, 0, free, data);
    data->enclosed = 0;
    data->group = group;

    return group;
}

static VALUE
thgroup_list(group)
    VALUE group;
{
    struct thgroup *data;
    rb_thread_t th;
    VALUE ary;

    Data_Get_Struct(group, struct thgroup, data);
    ary = rb_ary_new();

    FOREACH_THREAD(th) {
	if (th->thgroup == data->group) {
	    rb_ary_push(ary, th->thread);
	}
    }
    END_FOREACH(th);

    return ary;
}

VALUE
thgroup_enclose(group)
    VALUE group;
{
    struct thgroup *data;

    Data_Get_Struct(group, struct thgroup, data);
    data->enclosed = 1;

    return group;
}

static VALUE
thgroup_enclosed_p(group)
    VALUE group;
{
    struct thgroup *data;

    Data_Get_Struct(group, struct thgroup, data);
    if (data->enclosed) return Qtrue;
    return Qfalse;
}

static VALUE
thgroup_add(group, thread)
    VALUE group, thread;
{
    rb_thread_t th;
    struct thgroup *data;

    rb_secure(4);
    th = rb_thread_check(thread);

    if (OBJ_FROZEN(group)) {
      rb_raise(rb_eThreadError, "can't move to the frozen thread group");
    }
    Data_Get_Struct(group, struct thgroup, data);
    if (data->enclosed) {
	rb_raise(rb_eThreadError, "can't move to the enclosed thread group");
    }

    if (!th->thgroup) {
	return Qnil;
    }
    if (OBJ_FROZEN(th->thgroup)) {
	rb_raise(rb_eThreadError, "can't move from the frozen thread group");
    }
    Data_Get_Struct(th->thgroup, struct thgroup, data);
    if (data->enclosed) {
	rb_raise(rb_eThreadError, "can't move from the enclosed thread group");
    }

    th->thgroup = group;
    return group;
}

void
Init_Thread()
{
    VALUE cThGroup;

    rb_eThreadError = rb_define_class("ThreadError", rb_eStandardError);
    rb_cThread = rb_define_class("Thread", rb_cObject);
    rb_undef_alloc_func(rb_cThread);

    rb_define_singleton_method(rb_cThread, "new", rb_thread_s_new, -1);
    rb_define_method(rb_cThread, "initialize", rb_thread_initialize, -2);
    rb_define_singleton_method(rb_cThread, "start", rb_thread_start, -2);
    rb_define_singleton_method(rb_cThread, "fork", rb_thread_start, -2);

    rb_define_singleton_method(rb_cThread, "stop", rb_thread_stop, 0);
    rb_define_singleton_method(rb_cThread, "kill", rb_thread_s_kill, 1);
    rb_define_singleton_method(rb_cThread, "exit", rb_thread_exit, 0);
    rb_define_singleton_method(rb_cThread, "pass", rb_thread_pass, 0);
    rb_define_singleton_method(rb_cThread, "current", rb_thread_current, 0);
    rb_define_singleton_method(rb_cThread, "main", rb_thread_main, 0);
    rb_define_singleton_method(rb_cThread, "list", rb_thread_list, 0);

    rb_define_singleton_method(rb_cThread, "critical", rb_thread_critical_get, 0);
    rb_define_singleton_method(rb_cThread, "critical=", rb_thread_critical_set, 1);

    rb_define_singleton_method(rb_cThread, "abort_on_exception", rb_thread_s_abort_exc, 0);
    rb_define_singleton_method(rb_cThread, "abort_on_exception=", rb_thread_s_abort_exc_set, 1);

    rb_define_method(rb_cThread, "run", rb_thread_run, 0);
    rb_define_method(rb_cThread, "wakeup", rb_thread_wakeup, 0);
    rb_define_method(rb_cThread, "kill", rb_thread_kill, 0);
    rb_define_method(rb_cThread, "terminate", rb_thread_kill, 0);
    rb_define_method(rb_cThread, "exit", rb_thread_kill, 0);
    rb_define_method(rb_cThread, "value", rb_thread_value, 0);
    rb_define_method(rb_cThread, "status", rb_thread_status, 0);
    rb_define_method(rb_cThread, "join", rb_thread_join_m, -1);
    rb_define_method(rb_cThread, "alive?", rb_thread_alive_p, 0);
    rb_define_method(rb_cThread, "stop?", rb_thread_stop_p, 0);
    rb_define_method(rb_cThread, "raise", rb_thread_raise_m, -1);

    rb_define_method(rb_cThread, "abort_on_exception", rb_thread_abort_exc, 0);
    rb_define_method(rb_cThread, "abort_on_exception=", rb_thread_abort_exc_set, 1);

    rb_define_method(rb_cThread, "priority", rb_thread_priority, 0);
    rb_define_method(rb_cThread, "priority=", rb_thread_priority_set, 1);
    rb_define_method(rb_cThread, "safe_level", rb_thread_safe_level, 0);
    rb_define_method(rb_cThread, "group", rb_thread_group, 0);

    rb_define_method(rb_cThread, "[]", rb_thread_aref, 1);
    rb_define_method(rb_cThread, "[]=", rb_thread_aset, 2);
    rb_define_method(rb_cThread, "key?", rb_thread_key_p, 1);
    rb_define_method(rb_cThread, "keys", rb_thread_keys, 0);

    rb_define_method(rb_cThread, "inspect", rb_thread_inspect, 0);

    rb_cCont = rb_define_class("Continuation", rb_cObject);
    rb_undef_alloc_func(rb_cCont);
    rb_undef_method(CLASS_OF(rb_cCont), "new");
    rb_define_method(rb_cCont, "call", rb_cont_call, -1);
    rb_define_global_function("callcc", rb_callcc, 0);

    cThGroup = rb_define_class("ThreadGroup", rb_cObject);
    rb_define_alloc_func(cThGroup, thgroup_s_alloc);
    rb_define_method(cThGroup, "list", thgroup_list, 0);
    rb_define_method(cThGroup, "enclose", thgroup_enclose, 0);
    rb_define_method(cThGroup, "enclosed?", thgroup_enclosed_p, 0);
    rb_define_method(cThGroup, "add", thgroup_add, 1);
    thgroup_default = rb_obj_alloc(cThGroup);
    rb_define_const(cThGroup, "Default", thgroup_default);

    /* allocate main thread */
    main_thread = rb_thread_alloc(rb_cThread);
    curr_thread = main_thread->prev = main_thread->next = main_thread;
}

static VALUE
rb_f_catch(dmy, tag)
    VALUE dmy, tag;
{
    int state;
    ID t;
    VALUE val;			/* OK */

    t = rb_to_id(tag);
    PUSH_TAG(t);
    if ((state = EXEC_TAG()) == 0) {
	val = rb_yield_0(tag, 0, 0, Qfalse, Qfalse);
    }
    else if (state == TAG_THROW && t == prot_tag->dst) {
	val = prot_tag->retval;
	state = 0;
    }
    POP_TAG();
    if (state) JUMP_TAG(state);

    return val;
}

static VALUE
catch_i(tag)
    ID tag;
{
    return rb_funcall(Qnil, rb_intern("catch"), 1, ID2SYM(tag));
}

VALUE
rb_catch(tag, func, data)
    const char *tag;
    VALUE (*func)();
    VALUE data;
{
    return rb_iterate((VALUE(*)_((VALUE)))catch_i, rb_intern(tag), func, data);
}

static VALUE
rb_f_throw(argc, argv)
    int argc;
    VALUE *argv;
{
    VALUE tag, value;
    ID t;
    struct tag *tt = prot_tag;

    rb_scan_args(argc, argv, "11", &tag, &value);
    t = rb_to_id(tag);

    while (tt) {
	if (tt->tag == t) {
	    tt->dst = t;
	    break;
	}
	if (tt->tag == PROT_THREAD) {
	    rb_raise(rb_eThreadError, "uncaught throw `%s' in thread 0x%lx",
		     rb_id2name(t),
		     curr_thread);
	}
	tt = tt->prev;
    }
    if (!tt) {
	rb_name_error(t, "uncaught throw `%s'", rb_id2name(t));
    }
    return_value(value);
    rb_trap_restore_mask();
    JUMP_TAG(TAG_THROW);
#ifndef __GNUC__
    return Qnil; 		/* not reached */
#endif
}

void
rb_throw(tag, val)
    const char *tag;
    VALUE val;
{
    VALUE argv[2];
    ID t = rb_intern(tag);

    argv[0] = ID2SYM(t);
    argv[1] = val;
    rb_f_throw(2, argv);
}

static void
return_check()
{
    struct tag *tt = prot_tag;

    while (tt) {
	if (tt->tag == PROT_FUNC) {
	    break;
	}
	if (tt->tag == PROT_THREAD) {
	    rb_raise(rb_eThreadError, "return from within thread 0x%lx",
		     curr_thread);
	}
	tt = tt->prev;
    }
}
