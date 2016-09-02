/**********************************************************************

  object.c -

  $Author: melville $
  $Date: 2003/10/15 10:11:46 $
  created at: Thu Jul 15 12:01:24 JST 1993

  Copyright (C) 1993-2003 Yukihiro Matsumoto
  Copyright (C) 2000  Network Applied Communication Laboratory, Inc.
  Copyright (C) 2000  Information-technology Promotion Agency, Japan

**********************************************************************/

#include "ruby.h"
#include "st.h"
#include "util.h"
#include <stdio.h>
#include <errno.h>
#include <ctype.h>
#include <math.h>

VALUE rb_mKernel;
VALUE rb_cObject;
VALUE rb_cModule;
VALUE rb_cClass;
VALUE rb_cData;

VALUE rb_cNilClass;
VALUE rb_cTrueClass;
VALUE rb_cFalseClass;
VALUE rb_cSymbol;

static ID id_eq, id_eql, id_inspect, id_init_copy;

VALUE
rb_equal(obj1, obj2)
    VALUE obj1, obj2;
{
    VALUE result;

    if (obj1 == obj2) return Qtrue;
    result = rb_funcall(obj1, id_eq, 1, obj2);
    if (RTEST(result)) return Qtrue;
    return Qfalse;
}

int
rb_eql(obj1, obj2)
    VALUE obj1, obj2;
{
    return RTEST(rb_funcall(obj1, id_eql, 1, obj2));
}

static VALUE
rb_obj_equal(obj1, obj2)
    VALUE obj1, obj2;
{
    if (obj1 == obj2) return Qtrue;
    return Qfalse;
}

VALUE
rb_obj_id(obj)
    VALUE obj;
{
    if (SPECIAL_CONST_P(obj)) {
	return LONG2NUM((long)obj);
    }
    return (VALUE)((long)obj|FIXNUM_FLAG);
}

VALUE
rb_obj_id_obsolete(obj)
    VALUE obj;
{
    rb_warning("Object#id will be deprecated; use Object#object_id");
    return rb_obj_id(obj);
}

VALUE
rb_class_real(cl)
    VALUE cl;
{
    while (FL_TEST(cl, FL_SINGLETON) || TYPE(cl) == T_ICLASS) {
	cl = RCLASS(cl)->super;
    }
    return cl;
}

VALUE
rb_obj_type(obj)
    VALUE obj;
{
    rb_warn("Object#type is deprecated; use Object#class");
    return rb_class_real(CLASS_OF(obj));
}

VALUE
rb_obj_class(obj)
    VALUE obj;
{
    return rb_class_real(CLASS_OF(obj));
}

static void
init_copy(dest, obj)
    VALUE dest, obj;
{
    if (OBJ_FROZEN(dest)) {
        rb_raise(rb_eTypeError, "[bug] frozen object (%s) allocated", rb_obj_classname(dest));
    }
    RBASIC(dest)->flags &= ~(T_MASK|FL_EXIVAR);
    RBASIC(dest)->flags |= RBASIC(obj)->flags & (T_MASK|FL_EXIVAR|FL_TAINT);
    if (FL_TEST(obj, FL_EXIVAR)) {
	rb_copy_generic_ivar(dest, obj);
    }
    rb_gc_copy_finalizer(dest, obj);
    switch (TYPE(obj)) {
      case T_OBJECT:
      case T_CLASS:
      case T_MODULE:
	if (ROBJECT(dest)->iv_tbl) {
	    st_free_table(ROBJECT(dest)->iv_tbl);
	    ROBJECT(dest)->iv_tbl = 0;
	}
	if (ROBJECT(obj)->iv_tbl) {
	    ROBJECT(dest)->iv_tbl = st_copy(ROBJECT(obj)->iv_tbl);
	}
    }
    rb_funcall(dest, id_init_copy, 1, obj);
}

VALUE
rb_obj_clone(obj)
    VALUE obj;
{
    VALUE clone;

    if (rb_special_const_p(obj)) {
        rb_raise(rb_eTypeError, "can't clone %s", rb_obj_classname(obj));
    }
    clone = rb_obj_alloc(rb_obj_class(obj));
    RBASIC(clone)->klass = rb_singleton_class_clone(obj);
    RBASIC(clone)->flags = (RBASIC(obj)->flags | FL_TEST(clone, FL_TAINT)) & ~FL_FREEZE;
    init_copy(clone, obj);
    RBASIC(clone)->flags |= RBASIC(obj)->flags & FL_FREEZE;

    return clone;
}

VALUE
rb_obj_dup(obj)
    VALUE obj;
{
    VALUE dup;

    if (rb_special_const_p(obj)) {
        rb_raise(rb_eTypeError, "can't dup %s", rb_obj_classname(obj));
    }
    dup = rb_obj_alloc(rb_obj_class(obj));
    init_copy(dup, obj);

    return dup;
}

VALUE
rb_obj_init_copy(obj, orig)
    VALUE obj, orig;
{
    if (obj == orig) return obj;
    rb_check_frozen(obj);
    if (TYPE(obj) != TYPE(orig) || rb_obj_class(obj) != rb_obj_class(orig)) {
	rb_raise(rb_eTypeError, "initialize_copy should take same class object");
    }
    return obj;
}

static VALUE
rb_any_to_a(obj)
    VALUE obj;
{
    rb_warn("default `to_a' will be obsolete");
    return rb_ary_new3(1, obj);
}

VALUE
rb_any_to_s(obj)
    VALUE obj;
{
    char *cname = rb_obj_classname(obj);
    VALUE str;

    str = rb_str_new(0, strlen(cname)+6+16+1); /* 6:tags 16:addr 1:nul */
    sprintf(RSTRING(str)->ptr, "#<%s:0x%lx>", cname, obj);
    RSTRING(str)->len = strlen(RSTRING(str)->ptr);
    if (OBJ_TAINTED(obj)) OBJ_TAINT(str);

    return str;
}

VALUE
rb_inspect(obj)
    VALUE obj;
{
    return rb_obj_as_string(rb_funcall(obj, id_inspect, 0, 0));
}

static int
inspect_i(id, value, str)
    ID id;
    VALUE value;
    VALUE str;
{
    VALUE str2;
    char *ivname;

    /* need not to show internal data */
    if (CLASS_OF(value) == 0) return ST_CONTINUE;
    if (!rb_is_instance_id(id)) return ST_CONTINUE;
    if (RSTRING(str)->ptr[0] == '-') { /* first element */
	RSTRING(str)->ptr[0] = '#';
	rb_str_cat2(str, " ");
    }
    else {
	rb_str_cat2(str, ", ");
    }
    ivname = rb_id2name(id);
    rb_str_cat2(str, ivname);
    rb_str_cat2(str, "=");
    str2 = rb_inspect(value);
    rb_str_append(str, str2);
    OBJ_INFECT(str, str2);

    return ST_CONTINUE;
}

static VALUE
inspect_obj(obj, str)
    VALUE obj, str;
{
    st_foreach(ROBJECT(obj)->iv_tbl, inspect_i, str);
    rb_str_cat2(str, ">");
    RSTRING(str)->ptr[0] = '#';
    OBJ_INFECT(str, obj);

    return str;
}

static VALUE
rb_obj_inspect(obj)
    VALUE obj;
{
    if (TYPE(obj) == T_OBJECT
	&& ROBJECT(obj)->iv_tbl
	&& ROBJECT(obj)->iv_tbl->num_entries > 0) {
	VALUE str;
	char *c;

	c = rb_obj_classname(obj);
	if (rb_inspecting_p(obj)) {
	    str = rb_str_new(0, strlen(c)+10+16+1); /* 10:tags 16:addr 1:nul */
	    sprintf(RSTRING(str)->ptr, "#<%s:0x%lx ...>", c, obj);
	    RSTRING(str)->len = strlen(RSTRING(str)->ptr);
	    return str;
	}
	str = rb_str_new(0, strlen(c)+6+16+1); /* 6:tags 16:addr 1:nul */
	sprintf(RSTRING(str)->ptr, "-<%s:0x%lx", c, obj);
	RSTRING(str)->len = strlen(RSTRING(str)->ptr);
	return rb_protect_inspect(inspect_obj, obj, str);
    }
    return rb_funcall(obj, rb_intern("to_s"), 0, 0);
}

VALUE
rb_obj_is_instance_of(obj, c)
    VALUE obj, c;
{
    switch (TYPE(c)) {
      case T_MODULE:
      case T_CLASS:
      case T_ICLASS:
	break;
      default:
	rb_raise(rb_eTypeError, "class or module required");
    }

    if (rb_obj_class(obj) == c) return Qtrue;
    return Qfalse;
}

VALUE
rb_obj_is_kind_of(obj, c)
    VALUE obj, c;
{
    VALUE cl = CLASS_OF(obj);

    switch (TYPE(c)) {
      case T_MODULE:
      case T_CLASS:
      case T_ICLASS:
	break;

      default:
	rb_raise(rb_eTypeError, "class or module required");
    }

    while (cl) {
	if (cl == c || RCLASS(cl)->m_tbl == RCLASS(c)->m_tbl)
	    return Qtrue;
	cl = RCLASS(cl)->super;
    }
    return Qfalse;
}

static VALUE
rb_obj_dummy()
{
    return Qnil;
}

VALUE
rb_obj_tainted(obj)
    VALUE obj;
{
    if (OBJ_TAINTED(obj))
	return Qtrue;
    return Qfalse;
}

VALUE
rb_obj_taint(obj)
    VALUE obj;
{
    rb_secure(4);
    if (!OBJ_TAINTED(obj)) {
	if (OBJ_FROZEN(obj)) {
	    rb_error_frozen("object");
	}
	OBJ_TAINT(obj);
    }
    return obj;
}

VALUE
rb_obj_untaint(obj)
    VALUE obj;
{
    rb_secure(3);
    if (OBJ_TAINTED(obj)) {
	if (OBJ_FROZEN(obj)) {
	    rb_error_frozen("object");
	}
	FL_UNSET(obj, FL_TAINT);
    }
    return obj;
}

void
rb_obj_infect(obj1, obj2)
    VALUE obj1, obj2;
{
    OBJ_INFECT(obj1, obj2);
}

VALUE
rb_obj_freeze(obj)
    VALUE obj;
{
    if (!OBJ_FROZEN(obj)) {
	if (rb_safe_level() >= 4 && !OBJ_TAINTED(obj)) {
	    rb_raise(rb_eSecurityError, "Insecure: can't freeze object");
	}
	OBJ_FREEZE(obj);
    }
    return obj;
}

static VALUE
rb_obj_frozen_p(obj)
    VALUE obj;
{
    if (OBJ_FROZEN(obj)) return Qtrue;
    return Qfalse;
}

static VALUE
nil_to_i(obj)
    VALUE obj;
{
    return INT2FIX(0);
}

static VALUE
nil_to_f(obj)
    VALUE obj;
{
    return rb_float_new(0.0);
}

static VALUE
nil_to_s(obj)
    VALUE obj;
{
    return rb_str_new2("");
}

static VALUE
nil_to_a(obj)
    VALUE obj;
{
    return rb_ary_new2(0);
}

static VALUE
nil_inspect(obj)
    VALUE obj;
{
    return rb_str_new2("nil");
}

#ifdef NIL_PLUS
static VALUE
nil_plus(x, y)
    VALUE x, y;
{
    switch (TYPE(y)) {
      case T_NIL:
      case T_FIXNUM:
      case T_FLOAT:
      case T_BIGNUM:
      case T_STRING:
      case T_ARRAY:
	return y;
      default:
	rb_raise(rb_eTypeError, "tried to add %s(%s) to nil",
		 RSTRING(rb_inspect(y))->ptr,
		 rb_obj_classname(y));
    }
    /* not reached */
}
#endif

static VALUE
main_to_s(obj)
    VALUE obj;
{
    return rb_str_new2("main");
}

static VALUE
true_to_s(obj)
    VALUE obj;
{
    return rb_str_new2("true");
}

static VALUE
true_and(obj, obj2)
    VALUE obj, obj2;
{
    return RTEST(obj2)?Qtrue:Qfalse;
}

static VALUE
true_or(obj, obj2)
    VALUE obj, obj2;
{
    return Qtrue;
}

static VALUE
true_xor(obj, obj2)
    VALUE obj, obj2;
{
    return RTEST(obj2)?Qfalse:Qtrue;
}

static VALUE
false_to_s(obj)
    VALUE obj;
{
    return rb_str_new2("false");
}

static VALUE
false_and(obj, obj2)
    VALUE obj, obj2;
{
    return Qfalse;
}

static VALUE
false_or(obj, obj2)
    VALUE obj, obj2;
{
    return RTEST(obj2)?Qtrue:Qfalse;
}

static VALUE
false_xor(obj, obj2)
    VALUE obj, obj2;
{
    return RTEST(obj2)?Qtrue:Qfalse;
}

static VALUE
rb_true(obj)
    VALUE obj;
{
    return Qtrue;
}

static VALUE
rb_false(obj)
    VALUE obj;
{
    return Qfalse;
}

static VALUE
sym_to_i(sym)
    VALUE sym;
{
    ID id = SYM2ID(sym);

    return LONG2FIX(id);
}

static VALUE
sym_inspect(sym)
    VALUE sym;
{
    VALUE str;
    char *name;
    ID id = SYM2ID(sym);

    name = rb_id2name(id);
    str = rb_str_new(0, strlen(name)+1);
    RSTRING(str)->ptr[0] = ':';
    strcpy(RSTRING(str)->ptr+1, name);
    if (rb_is_junk_id(id)) {
	str = rb_str_dump(str);
	strncpy(RSTRING(str)->ptr, ":\"", 2);
    }
    return str;
}

static VALUE
sym_to_s(sym)
    VALUE sym;
{
    return rb_str_new2(rb_id2name(SYM2ID(sym)));
}

static VALUE
sym_to_sym(sym)
    VALUE sym;
{
    return sym;
}

static VALUE
rb_mod_to_s(klass)
    VALUE klass;

{
    if (FL_TEST(klass, FL_SINGLETON)) {
	VALUE s = rb_str_new2("#<");
	VALUE v = rb_iv_get(klass, "__attached__");

	rb_str_cat2(s, "Class:");
	switch (TYPE(v)) {
	  case T_CLASS: case T_MODULE:
	    rb_str_append(s, rb_inspect(v));
	    break;
	  default:
	    rb_str_append(s, rb_any_to_s(v));
	    break;
	}
	rb_str_cat2(s, ">");

	return s;
    }
    return rb_str_dup(rb_class_path(klass));
}

static VALUE
rb_mod_eqq(mod, arg)
    VALUE mod, arg;
{
    return rb_obj_is_kind_of(arg, mod);
}

static VALUE
rb_mod_le(mod, arg)
    VALUE mod, arg;
{
    VALUE start = mod;

    if (mod == arg) return Qtrue;
    switch (TYPE(arg)) {
      case T_MODULE:
      case T_CLASS:
	break;
      default:
	rb_raise(rb_eTypeError, "compared with non class/module");
    }

    while (mod) {
	if (RCLASS(mod)->m_tbl == RCLASS(arg)->m_tbl)
	    return Qtrue;
	mod = RCLASS(mod)->super;
    }
    /* not mod < arg; check if mod > arg */
    while (arg) {
	if (RCLASS(arg)->m_tbl == RCLASS(start)->m_tbl)
	    return Qfalse;
	arg = RCLASS(arg)->super;
    }
    return Qnil;
}

static VALUE
rb_mod_lt(mod, arg)
    VALUE mod, arg;
{
    if (mod == arg) return Qfalse;
    return rb_mod_le(mod, arg);
}

static VALUE
rb_mod_ge(mod, arg)
    VALUE mod, arg;
{
    switch (TYPE(arg)) {
      case T_MODULE:
      case T_CLASS:
	break;
      default:
	rb_raise(rb_eTypeError, "compared with non class/module");
    }

    return rb_mod_le(arg, mod);
}

static VALUE
rb_mod_gt(mod, arg)
    VALUE mod, arg;
{
    if (mod == arg) return Qfalse;
    return rb_mod_ge(mod, arg);
}

static VALUE
rb_mod_cmp(mod, arg)
    VALUE mod, arg;
{
    VALUE cmp;

    if (mod == arg) return INT2FIX(0);
    switch (TYPE(arg)) {
      case T_MODULE:
      case T_CLASS:
	break;
      default:
	return Qnil;
    }

    cmp = rb_mod_le(mod, arg);
    if (NIL_P(cmp)) return Qnil;
    if (cmp) {
	return INT2FIX(-1);
    }
    return INT2FIX(1);
}

static VALUE
rb_mod_initialize(module)
    VALUE module;
{
    if (rb_block_given_p()) {
	rb_mod_module_eval(0, 0, module);
    }
    return Qnil;
}

static VALUE
rb_class_initialize(argc, argv, klass)
    int argc;
    VALUE *argv;
    VALUE klass;
{
    return rb_mod_initialize(klass);
}

static VALUE rb_module_s_alloc _((VALUE));
static VALUE
rb_module_s_alloc(klass)
    VALUE klass;
{
    VALUE mod = rb_module_new();

    RBASIC(mod)->klass = klass;
    return mod;
}

static VALUE
rb_class_s_new(argc, argv)
    int argc;
    VALUE *argv;
{
    VALUE super, klass;

    if (rb_scan_args(argc, argv, "01", &super) == 0) {
	super = rb_cObject;
    }
    klass = rb_class_new(super);
    rb_make_metaclass(klass, RBASIC(super)->klass);
    rb_obj_call_init(klass, argc, argv);
    rb_class_inherited(super, klass);

    return klass;
}

VALUE
rb_obj_alloc(klass)
    VALUE klass;
{
    VALUE obj;

    if (FL_TEST(klass, FL_SINGLETON)) {
	rb_raise(rb_eTypeError, "can't create instance of virtual class");
    }
    obj = rb_funcall(klass, ID_ALLOCATOR, 0, 0);
    if (rb_obj_class(obj) != rb_class_real(klass)) {
	rb_raise(rb_eTypeError, "wrong instance allocation");
    }
    return obj;
}

static VALUE rb_class_allocate_instance _((VALUE));
static VALUE
rb_class_allocate_instance(klass)
    VALUE klass;
{
    NEWOBJ(obj, struct RObject);
    OBJSETUP(obj, klass, T_OBJECT);
    return (VALUE)obj;
}

VALUE
rb_class_new_instance(argc, argv, klass)
    int argc;
    VALUE *argv;
    VALUE klass;
{
    VALUE obj;

    obj = rb_obj_alloc(klass);
    rb_obj_call_init(obj, argc, argv);

    return obj;
}

static VALUE
rb_class_superclass(klass)
    VALUE klass;
{
    VALUE super = RCLASS(klass)->super;

    while (TYPE(super) == T_ICLASS) {
	super = RCLASS(super)->super;
    }
    if (!super) {
	return Qnil;
    }
    return super;
}

static ID
str_to_id(str)
    VALUE str;
{
    if (!RSTRING(str)->ptr || RSTRING(str)->len == 0) {
	rb_raise(rb_eArgError, "empty symbol string");
    }
    return rb_intern(RSTRING(str)->ptr);
}

ID
rb_to_id(name)
    VALUE name;
{
    VALUE tmp;
    ID id;

    switch (TYPE(name)) {
      case T_STRING:
	return str_to_id(name);
      case T_FIXNUM:
	rb_warn("do not use Fixnums as Symbols");
	id = FIX2LONG(name);
	if (!rb_id2name(id)) {
	    rb_raise(rb_eArgError, "%ld is not a symbol", id);
	}
	break;
      case T_SYMBOL:
	id = SYM2ID(name);
	break;
      default:
	tmp = rb_check_string_type(name);
	if (!NIL_P(tmp)) {
	    return str_to_id(tmp);
	}
	rb_raise(rb_eTypeError, "%s is not a symbol", RSTRING(rb_inspect(name))->ptr);
    }
    return id;
}

static VALUE
rb_mod_attr(argc, argv, klass)
    int argc;
    VALUE *argv;
    VALUE klass;
{
    VALUE name, pub;

    rb_scan_args(argc, argv, "11", &name, &pub);
    rb_attr(klass, rb_to_id(name), 1, RTEST(pub), Qtrue);
    return Qnil;
}

static VALUE
rb_mod_attr_reader(argc, argv, klass)
    int argc;
    VALUE *argv;
    VALUE klass;
{
    int i;

    for (i=0; i<argc; i++) {
	rb_attr(klass, rb_to_id(argv[i]), 1, 0, Qtrue);
    }
    return Qnil;
}

static VALUE
rb_mod_attr_writer(argc, argv, klass)
    int argc;
    VALUE *argv;
    VALUE klass;
{
    int i;

    for (i=0; i<argc; i++) {
	rb_attr(klass, rb_to_id(argv[i]), 0, 1, Qtrue);
    }
    return Qnil;
}

static VALUE
rb_mod_attr_accessor(argc, argv, klass)
    int argc;
    VALUE *argv;
    VALUE klass;
{
    int i;

    for (i=0; i<argc; i++) {
	rb_attr(klass, rb_to_id(argv[i]), 1, 1, Qtrue);
    }
    return Qnil;
}

static VALUE
rb_mod_const_get(mod, name)
    VALUE mod, name;
{
    ID id = rb_to_id(name);

    if (!rb_is_const_id(id)) {
	rb_name_error(id, "wrong constant name %s", rb_id2name(id));
    }
    return rb_const_get(mod, id);
}

static VALUE
rb_mod_const_set(mod, name, value)
    VALUE mod, name, value;
{
    ID id = rb_to_id(name);

    if (!rb_is_const_id(id)) {
	rb_name_error(id, "wrong constant name %s", rb_id2name(id));
    }
    rb_const_set(mod, id, value);
    return value;
}

static VALUE
rb_mod_const_defined(mod, name)
    VALUE mod, name;
{
    ID id = rb_to_id(name);

    if (!rb_is_const_id(id)) {
	rb_name_error(id, "wrong constant name %s", rb_id2name(id));
    }
    return rb_const_defined_at(mod, id);
}

static VALUE
rb_obj_methods(argc, argv, obj)
    int argc;
    VALUE *argv;
    VALUE obj;
{
  retry:
    if (argc == 0) {
	VALUE args[1];

	args[0] = Qtrue;
	return rb_class_instance_methods(1, args, CLASS_OF(obj));
    }
    else {
	VALUE recur;

	rb_scan_args(argc, argv, "1", &recur);
	if (RTEST(recur)) {
	    argc = 0;
	    goto retry;
	}
	return rb_obj_singleton_methods(argc, argv, obj);
    }
}

static VALUE
rb_obj_protected_methods(argc, argv, obj)
    int argc;
    VALUE *argv;
    VALUE obj;
{
    if (argc == 0) {		/* hack to stop warning */
	VALUE args[1];

	args[0] = Qtrue;
	return rb_class_protected_instance_methods(1, args, CLASS_OF(obj));
    }
    return rb_class_protected_instance_methods(argc, argv, CLASS_OF(obj));
}

static VALUE
rb_obj_private_methods(argc, argv, obj)
    int argc;
    VALUE *argv;
    VALUE obj;
{
    if (argc == 0) {		/* hack to stop warning */
	VALUE args[1];

	args[0] = Qtrue;
	return rb_class_private_instance_methods(1, args, CLASS_OF(obj));
    }
    return rb_class_private_instance_methods(argc, argv, CLASS_OF(obj));
}

static VALUE
rb_obj_public_methods(argc, argv, obj)
    int argc;
    VALUE *argv;
    VALUE obj;
{
    if (argc == 0) {		/* hack to stop warning */
	VALUE args[1];

	args[0] = Qtrue;
	return rb_class_public_instance_methods(1, args, CLASS_OF(obj));
    }
    return rb_class_public_instance_methods(argc, argv, CLASS_OF(obj));
}

static VALUE
rb_obj_ivar_get(obj, iv)
    VALUE obj, iv;
{
    ID id = rb_to_id(iv);

    if (!rb_is_instance_id(id)) {
	rb_name_error(id, "`%s' is not allowed as an instance variable name", rb_id2name(id));
    }
    return rb_ivar_get(obj, id);
}

static VALUE
rb_obj_ivar_set(obj, iv, val)
    VALUE obj, iv, val;
{
    ID id = rb_to_id(iv);

    if (!rb_is_instance_id(id)) {
	rb_name_error(id, "`%s' is not allowed as an instance variable name", rb_id2name(id));
    }
    return rb_ivar_set(obj, id, val);
}

static VALUE
convert_type(val, tname, method, raise)
    VALUE val;
    const char *tname, *method;
    int raise;
{
    ID m;

    m = rb_intern(method);
    if (!rb_respond_to(val, m)) {
	if (raise) {
	    rb_raise(rb_eTypeError, "cannot convert %s into %s",
		     NIL_P(val) ? "nil" :
		     val == Qtrue ? "true" :
		     val == Qfalse ? "false" :
		     rb_obj_classname(val), 
		     tname);
	}
	else {
	    return Qnil;
	}
    }
    return rb_funcall(val, m, 0);
}

VALUE
rb_convert_type(val, type, tname, method)
    VALUE val;
    int type;
    const char *tname, *method;
{
    VALUE v;

    if (TYPE(val) == type) return val;
    v = convert_type(val, tname, method, Qtrue);
    if (TYPE(v) != type) {
	rb_raise(rb_eTypeError, "%s#%s should return %s",
		 rb_obj_classname(val), method, tname);
    }
    return v;
}

VALUE
rb_check_convert_type(val, type, tname, method)
    VALUE val;
    int type;
    const char *tname, *method;
{
    VALUE v;

    /* always convert T_DATA */
    if (TYPE(val) == type && type != T_DATA) return val;
    v = convert_type(val, tname, method, Qfalse);
    if (NIL_P(v)) return Qnil;
    if (TYPE(v) != type) {
	rb_raise(rb_eTypeError, "%s#%s should return %s",
		 rb_obj_classname(val), method, tname);
    }
    return v;
}


static VALUE
rb_to_integer(val, method)
    VALUE val;
    char *method;
{
    VALUE v = convert_type(val, "Integer", method, Qtrue);
    if (!rb_obj_is_kind_of(v, rb_cInteger)) {
	rb_raise(rb_eTypeError, "%s#%s should return Integer",
		 rb_obj_classname(val), method);
    }
    return v;
}

VALUE
rb_to_int(val)
    VALUE val;
{
    return rb_to_integer(val, "to_int");
}

VALUE
rb_Integer(val)
    VALUE val;
{
    switch (TYPE(val)) {
      case T_FLOAT:
	if (RFLOAT(val)->value <= (double)FIXNUM_MAX
	    && RFLOAT(val)->value >= (double)FIXNUM_MIN) {
	    break;
	}
	return rb_dbl2big(RFLOAT(val)->value);

      case T_FIXNUM:
      case T_BIGNUM:
	return val;

      case T_STRING:
	return rb_str_to_inum(val, 0, Qtrue);

      default:
	break;
    }
    if (rb_respond_to(val, rb_intern("to_int"))) {
	return rb_to_integer(val, "to_int");
    }
    return rb_to_integer(val, "to_i");
}

static VALUE
rb_f_integer(obj, arg)
    VALUE obj, arg;
{
    return rb_Integer(arg);
}

double
rb_cstr_to_dbl(p, badcheck)
    const char *p;
    int badcheck;
{
    const char *q;
    char *end;
    double d;

    if (!p) return 0.0;
    q = p;
    if (badcheck) {
	while (ISSPACE(*p)) p++;
    }
    else {
	while (ISSPACE(*p) || *p == '_') p++;
    }
    d = strtod(p, &end);
    if (errno == ERANGE) {
	rb_warn("Float %*s out of range", end-p, p);
	errno = 0;
    }
    if (p == end) {
	if (badcheck) {
	  bad:
	    rb_invalid_str(q, "Float()");
	}
	return d;
    }
    if (*end) {
	char *buf = ALLOCA_N(char, strlen(p)+1);
	char *n = buf;

	while (p < end) *n++ = *p++;
	while (*p) {
	    if (*p == '_') {
		/* remove underscores between digits */
		if (badcheck) {
		    if (n == buf || !ISDIGIT(n[-1])) goto bad;
		    ++p;
		    if (!ISDIGIT(*p)) goto bad;
		}
		else {
		    while (*++p == '_');
		    continue;
		}
	    }
	    *n++ = *p++;
	}
	*n = '\0';
	p = buf;
	d = strtod(p, &end);
	if (errno == ERANGE) {
	    rb_warn("Float %*s out of range", end-p, p);
	    errno = 0;
	}
	if (badcheck) {
	    if (p == end) goto bad;
	    while (*end && ISSPACE(*end)) end++;
	    if (*end) goto bad;
	}
    }
    if (errno == ERANGE) {
	errno = 0;
	rb_raise(rb_eArgError, "Float %s out of range", q);
    }
    return d;
}

double
rb_str_to_dbl(str, badcheck)
    VALUE str;
    int badcheck;
{
    char *s;
    long len;

    StringValue(str);
    s = RSTRING(str)->ptr;
    len = RSTRING(str)->len;
    if (s) {
	if (s[len]) {		/* no sentinel somehow */
	    char *p = ALLOCA_N(char, len+1);

	    MEMCPY(p, s, char, len);
	    p[len] = '\0';
	    s = p;
	}
	if (badcheck && len != strlen(s)) {
	    rb_raise(rb_eArgError, "string for Float contains null byte");
	}
    }
    return rb_cstr_to_dbl(s, badcheck);
}

VALUE
rb_Float(val)
    VALUE val;
{
    switch (TYPE(val)) {
      case T_FIXNUM:
	return rb_float_new((double)FIX2LONG(val));

      case T_FLOAT:
	return val;

      case T_BIGNUM:
	return rb_float_new(rb_big2dbl(val));

      case T_STRING:
	return rb_float_new(rb_str_to_dbl(val, Qtrue));

      case T_NIL:
	rb_raise(rb_eTypeError, "cannot convert nil into Float");
	break;

      default:
      {
	  VALUE f = rb_convert_type(val, T_FLOAT, "Float", "to_f");
	  if (isnan(RFLOAT(f)->value)) {
	      rb_raise(rb_eArgError, "invalid value for Float()");
	  }
	  return f;
      }
    }
}

static VALUE
rb_f_float(obj, arg)
    VALUE obj, arg;
{
    return rb_Float(arg);
}

double
rb_num2dbl(val)
    VALUE val;
{
    switch (TYPE(val)) {
      case T_FLOAT:
	return RFLOAT(val)->value;

      case T_STRING:
	rb_raise(rb_eTypeError, "no implicit conversion to float from string");
	break;

      case T_NIL:
	rb_raise(rb_eTypeError, "no implicit conversion to float from nil");
	break;

      default:
	break;
    }

    return RFLOAT(rb_Float(val))->value;
}

char*
rb_str2cstr(str, len)
    VALUE str;
    long *len;
{
    StringValue(str);
    if (len) *len = RSTRING(str)->len;
    else if (RTEST(ruby_verbose) && RSTRING(str)->len != strlen(RSTRING(str)->ptr)) {
	rb_warn("string contains \\0 character");
    }
    return RSTRING(str)->ptr;
}

VALUE
rb_String(val)
    VALUE val;
{
    return rb_convert_type(val, T_STRING, "String", "to_s");
}

static VALUE
rb_f_string(obj, arg)
    VALUE obj, arg;
{
    return rb_String(arg);
}

#if 0
VALUE
rb_Array(val)
    VALUE val;
{
    VALUE tmp = rb_check_array_type(val);
    ID to_a;

    if (NIL_P(tmp)) {
	to_a = rb_intern("to_a");
	if (rb_respond_to(val, to_a)) {
	    val = rb_funcall(val, to_a, 0);
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
rb_f_array(obj, arg)
    VALUE obj, arg;
{
    return rb_Array(arg);
}

static VALUE
boot_defclass(name, super)
    char *name;
    VALUE super;
{
    extern st_table *rb_class_tbl;
    VALUE obj = rb_class_boot(super);
    ID id = rb_intern(name);

    rb_name_class(obj, id);
    st_add_direct(rb_class_tbl, id, obj);
    rb_const_set((rb_cObject ? rb_cObject : obj), id, obj);
    return obj;
}

VALUE ruby_top_self;

void
Init_Object()
{
    VALUE metaclass;

    rb_cObject = boot_defclass("Object", 0);
    rb_cModule = boot_defclass("Module", rb_cObject);
    rb_cClass =  boot_defclass("Class",  rb_cModule);

    metaclass = rb_make_metaclass(rb_cObject, rb_cClass);
    metaclass = rb_make_metaclass(rb_cModule, metaclass);
    metaclass = rb_make_metaclass(rb_cClass, metaclass);

    rb_mKernel = rb_define_module("Kernel");
    rb_include_module(rb_cObject, rb_mKernel);
    rb_define_alloc_func(rb_cObject, rb_class_allocate_instance);
    rb_define_private_method(rb_cObject, "initialize", rb_obj_dummy, 0);
    rb_define_private_method(rb_cClass, "inherited", rb_obj_dummy, 1);
    rb_define_private_method(rb_cModule, "included", rb_obj_dummy, 1);
    rb_define_private_method(rb_cModule, "method_added", rb_obj_dummy, 1);
    rb_define_private_method(rb_cModule, "method_removed", rb_obj_dummy, 1);
    rb_define_private_method(rb_cModule, "method_undefined", rb_obj_dummy, 1);

    /*
     * Ruby's Class Hierarchy Chart
     *
     *                           +------------------+
     *                           |                  |
     *             Object---->(Object)              |
     *              ^  ^        ^  ^                |
     *              |  |        |  |                |
     *              |  |  +-----+  +---------+      |
     *              |  |  |                  |      |
     *              |  +-----------+         |      |
     *              |     |        |         |      |
     *       +------+     |     Module--->(Module)  |
     *       |            |        ^         ^      |
     *  OtherClass-->(OtherClass)  |         |      |
     *                             |         |      |
     *                           Class---->(Class)  |
     *                             ^                |
     *                             |                |
     *                             +----------------+
     *
     *   + All metaclasses are instances of the class `Class'.
     */

    rb_define_method(rb_mKernel, "nil?", rb_false, 0);
    rb_define_method(rb_mKernel, "==", rb_obj_equal, 1);
    rb_define_method(rb_mKernel, "equal?", rb_obj_equal, 1);
    rb_define_method(rb_mKernel, "===", rb_equal, 1); 
    rb_define_method(rb_mKernel, "=~", rb_false, 1);

    rb_define_method(rb_mKernel, "eql?", rb_obj_equal, 1);

    rb_define_method(rb_mKernel, "hash", rb_obj_id, 0);
    rb_define_method(rb_mKernel, "id", rb_obj_id_obsolete, 0);
    rb_define_method(rb_mKernel, "__id__", rb_obj_id, 0);
    rb_define_method(rb_mKernel, "object_id", rb_obj_id, 0);
    rb_define_method(rb_mKernel, "type", rb_obj_type, 0);
    rb_define_method(rb_mKernel, "class", rb_obj_class, 0);

    rb_define_method(rb_mKernel, "clone", rb_obj_clone, 0);
    rb_define_method(rb_mKernel, "dup", rb_obj_dup, 0);
    rb_define_method(rb_mKernel, "initialize_copy", rb_obj_init_copy, 1);

    rb_define_method(rb_mKernel, "taint", rb_obj_taint, 0);
    rb_define_method(rb_mKernel, "tainted?", rb_obj_tainted, 0);
    rb_define_method(rb_mKernel, "untaint", rb_obj_untaint, 0);
    rb_define_method(rb_mKernel, "freeze", rb_obj_freeze, 0);
    rb_define_method(rb_mKernel, "frozen?", rb_obj_frozen_p, 0);

    rb_define_method(rb_mKernel, "to_a", rb_any_to_a, 0); /* to be removed */
    rb_define_method(rb_mKernel, "to_s", rb_any_to_s, 0);
    rb_define_method(rb_mKernel, "inspect", rb_obj_inspect, 0);
    rb_define_method(rb_mKernel, "methods", rb_obj_methods, -1);
    rb_define_method(rb_mKernel, "singleton_methods", rb_obj_singleton_methods, -1);
    rb_define_method(rb_mKernel, "protected_methods", rb_obj_protected_methods, -1);
    rb_define_method(rb_mKernel, "private_methods", rb_obj_private_methods, -1);
    rb_define_method(rb_mKernel, "public_methods", rb_obj_public_methods, -1);
    rb_define_method(rb_mKernel, "instance_variables", rb_obj_instance_variables, 0);
    rb_define_method(rb_mKernel, "instance_variable_get", rb_obj_ivar_get, 1);
    rb_define_method(rb_mKernel, "instance_variable_set", rb_obj_ivar_set, 2);
    rb_define_private_method(rb_mKernel, "remove_instance_variable",
			     rb_obj_remove_instance_variable, 1);

    rb_define_method(rb_mKernel, "instance_of?", rb_obj_is_instance_of, 1);
    rb_define_method(rb_mKernel, "kind_of?", rb_obj_is_kind_of, 1);
    rb_define_method(rb_mKernel, "is_a?", rb_obj_is_kind_of, 1);

    rb_define_private_method(rb_mKernel, "singleton_method_added", rb_obj_dummy, 1);
    rb_define_private_method(rb_mKernel, "singleton_method_removed", rb_obj_dummy, 1);
    rb_define_private_method(rb_mKernel, "singleton_method_undefined", rb_obj_dummy, 1);

    rb_define_global_function("sprintf", rb_f_sprintf, -1);
    rb_define_global_function("format", rb_f_sprintf, -1);

    rb_define_global_function("Integer", rb_f_integer, 1);
    rb_define_global_function("Float", rb_f_float, 1);

    rb_define_global_function("String", rb_f_string, 1);
    rb_define_global_function("Array", rb_f_array, 1);

    rb_cNilClass = rb_define_class("NilClass", rb_cObject);
    rb_define_method(rb_cNilClass, "to_i", nil_to_i, 0);
    rb_define_method(rb_cNilClass, "to_f", nil_to_f, 0);
    rb_define_method(rb_cNilClass, "to_s", nil_to_s, 0);
    rb_define_method(rb_cNilClass, "to_a", nil_to_a, 0);
    rb_define_method(rb_cNilClass, "inspect", nil_inspect, 0);
    rb_define_method(rb_cNilClass, "&", false_and, 1);
    rb_define_method(rb_cNilClass, "|", false_or, 1);
    rb_define_method(rb_cNilClass, "^", false_xor, 1);

    rb_define_method(rb_cNilClass, "nil?", rb_true, 0);
    rb_undef_alloc_func(rb_cNilClass);
    rb_undef_method(CLASS_OF(rb_cNilClass), "new");
    rb_define_global_const("NIL", Qnil);

    rb_cSymbol = rb_define_class("Symbol", rb_cObject);
    rb_define_singleton_method(rb_cSymbol, "all_symbols", rb_sym_all_symbols, 0);
    rb_undef_alloc_func(rb_cSymbol);
    rb_undef_method(CLASS_OF(rb_cSymbol), "new");

    rb_define_method(rb_cSymbol, "to_i", sym_to_i, 0);
    rb_define_method(rb_cSymbol, "to_int", sym_to_i, 0);
    rb_define_method(rb_cSymbol, "inspect", sym_inspect, 0);
    rb_define_method(rb_cSymbol, "to_s", sym_to_s, 0);
    rb_define_method(rb_cSymbol, "id2name", sym_to_s, 0);
    rb_define_method(rb_cSymbol, "to_sym", sym_to_sym, 0);

    rb_define_method(rb_cModule, "===", rb_mod_eqq, 1);
    rb_define_method(rb_cModule, "==", rb_obj_equal, 1);
    rb_define_method(rb_cModule, "<=>",  rb_mod_cmp, 1);
    rb_define_method(rb_cModule, "<",  rb_mod_lt, 1);
    rb_define_method(rb_cModule, "<=", rb_mod_le, 1);
    rb_define_method(rb_cModule, ">",  rb_mod_gt, 1);
    rb_define_method(rb_cModule, ">=", rb_mod_ge, 1);
    rb_define_method(rb_cModule, "clone", rb_mod_clone, 0);
    rb_define_method(rb_cModule, "dup", rb_mod_dup, 0);
    rb_define_method(rb_cModule, "to_s", rb_mod_to_s, 0);
    rb_define_method(rb_cModule, "included_modules", rb_mod_included_modules, 0);
    rb_define_method(rb_cModule, "include?", rb_mod_include_p, 1);
    rb_define_method(rb_cModule, "name", rb_mod_name, 0);
    rb_define_method(rb_cModule, "ancestors", rb_mod_ancestors, 0);

    rb_define_private_method(rb_cModule, "attr", rb_mod_attr, -1);
    rb_define_private_method(rb_cModule, "attr_reader", rb_mod_attr_reader, -1);
    rb_define_private_method(rb_cModule, "attr_writer", rb_mod_attr_writer, -1);
    rb_define_private_method(rb_cModule, "attr_accessor", rb_mod_attr_accessor, -1);

    rb_define_alloc_func(rb_cModule, rb_module_s_alloc);
    rb_define_method(rb_cModule, "initialize", rb_mod_initialize, 0);
    rb_define_method(rb_cModule, "instance_methods", rb_class_instance_methods, -1);
    rb_define_method(rb_cModule, "public_instance_methods", rb_class_public_instance_methods, -1);
    rb_define_method(rb_cModule, "protected_instance_methods", rb_class_protected_instance_methods, -1);
    rb_define_method(rb_cModule, "private_instance_methods", rb_class_private_instance_methods, -1);

    rb_define_method(rb_cModule, "constants", rb_mod_constants, 0);
    rb_define_method(rb_cModule, "const_get", rb_mod_const_get, 1);
    rb_define_method(rb_cModule, "const_set", rb_mod_const_set, 2);
    rb_define_method(rb_cModule, "const_defined?", rb_mod_const_defined, 1);
    rb_define_private_method(rb_cModule, "remove_const", rb_mod_remove_const, 1);
    rb_define_method(rb_cModule, "const_missing", rb_mod_const_missing, 1);
    rb_define_method(rb_cModule, "class_variables", rb_mod_class_variables, 0);
    rb_define_private_method(rb_cModule, "remove_class_variable", rb_mod_remove_cvar, 1);

    rb_define_method(rb_cClass, "allocate", rb_obj_alloc, 0);
    rb_define_method(rb_cClass, "new", rb_class_new_instance, -1);
    rb_define_method(rb_cClass, "initialize", rb_class_initialize, -1);
    rb_define_method(rb_cClass, "superclass", rb_class_superclass, 0);
    rb_undef_alloc_func(rb_cClass);
    rb_define_singleton_method(rb_cClass, "new", rb_class_s_new, -1);
    rb_undef_method(rb_cClass, "extend_object");
    rb_undef_method(rb_cClass, "append_features");

    rb_cData = rb_define_class("Data", rb_cObject);
    rb_undef_alloc_func(rb_cData);

    ruby_top_self = rb_obj_alloc(rb_cObject);
    rb_global_variable(&ruby_top_self);
    rb_define_singleton_method(ruby_top_self, "to_s", main_to_s, 0);

    rb_cTrueClass = rb_define_class("TrueClass", rb_cObject);
    rb_define_method(rb_cTrueClass, "to_s", true_to_s, 0);
    rb_define_method(rb_cTrueClass, "&", true_and, 1);
    rb_define_method(rb_cTrueClass, "|", true_or, 1);
    rb_define_method(rb_cTrueClass, "^", true_xor, 1);
    rb_undef_alloc_func(rb_cTrueClass);
    rb_undef_method(CLASS_OF(rb_cTrueClass), "new");
    rb_define_global_const("TRUE", Qtrue);

    rb_cFalseClass = rb_define_class("FalseClass", rb_cObject);
    rb_define_method(rb_cFalseClass, "to_s", false_to_s, 0);
    rb_define_method(rb_cFalseClass, "&", false_and, 1);
    rb_define_method(rb_cFalseClass, "|", false_or, 1);
    rb_define_method(rb_cFalseClass, "^", false_xor, 1);
    rb_undef_alloc_func(rb_cFalseClass);
    rb_undef_method(CLASS_OF(rb_cFalseClass), "new");
    rb_define_global_const("FALSE", Qfalse);

    id_eq = rb_intern("==");
    id_eql = rb_intern("eql?");
    id_inspect = rb_intern("inspect");
    id_init_copy = rb_intern("initialize_copy");
}
