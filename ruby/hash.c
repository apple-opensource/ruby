/**********************************************************************

  hash.c -

  $Author: melville $
  $Date: 2003/10/15 12:07:43 $
  created at: Mon Nov 22 18:51:18 JST 1993

  Copyright (C) 1993-2003 Yukihiro Matsumoto
  Copyright (C) 2000  Network Applied Communication Laboratory, Inc.
  Copyright (C) 2000  Information-technology Promotion Agency, Japan

**********************************************************************/

#include "ruby.h"
#include "st.h"
#include "util.h"
#include "rubysig.h"
#include "version.h"

#ifdef __APPLE__
#include <crt_externs.h>
#endif

#define HASH_DELETED  FL_USER1
#define HASH_PROC_DEFAULT FL_USER2

static void
rb_hash_modify(hash)
    VALUE hash;
{
    if (!RHASH(hash)->tbl) rb_raise(rb_eTypeError, "uninitialized Hash");
    if (OBJ_FROZEN(hash)) rb_error_frozen("hash");
    if (!OBJ_TAINTED(hash) && rb_safe_level() >= 4)
	rb_raise(rb_eSecurityError, "Insecure: can't modify hash");
}

VALUE
rb_hash_freeze(hash)
    VALUE hash;
{
    return rb_obj_freeze(hash);
}

VALUE rb_cHash;

static VALUE envtbl;
static ID id_hash, id_call, id_default;

VALUE
rb_hash(obj)
    VALUE obj;
{
    return rb_funcall(obj, id_hash, 0);
}

static VALUE
eql(args)
    VALUE *args;
{
    return (VALUE)rb_eql(args[0], args[1]);
}

static int
rb_any_cmp(a, b)
    VALUE a, b;
{
    VALUE args[2];

    if (a == b) return 0;
    if (FIXNUM_P(a) && FIXNUM_P(b)) {
	return a != b;
    }
    if (TYPE(a) == T_STRING && RBASIC(a)->klass == rb_cString &&
	TYPE(b) == T_STRING && RBASIC(b)->klass == rb_cString) {
	return rb_str_cmp(a, b);
    }
    if (a == Qundef || b == Qundef) return -1;
    if (SYMBOL_P(a) && SYMBOL_P(b)) {
	return a != b;
    }

    args[0] = a;
    args[1] = b;
    return !rb_with_disable_interrupt(eql, (VALUE)args);
}

static int
rb_any_hash(a)
    VALUE a;
{
    VALUE hval;

    switch (TYPE(a)) {
      case T_FIXNUM:
      case T_SYMBOL:
	return (int)a;
	break;

      case T_STRING:
	return rb_str_hash(a);
	break;

      default:
	hval = rb_funcall(a, id_hash, 0);
	if (!FIXNUM_P(hval)) {
	    hval = rb_funcall(hval, '%', 1, INT2FIX(536870923));
	}
	return (int)FIX2LONG(hval);
    }
}

static struct st_hash_type objhash = {
    rb_any_cmp,
    rb_any_hash,
};

struct rb_hash_foreach_arg {
    VALUE hash;
    enum st_retval (*func)();
    VALUE arg;
};

static int
rb_hash_foreach_iter(key, value, arg)
    VALUE key, value;
    struct rb_hash_foreach_arg *arg;
{
    int status;
    st_table *tbl = RHASH(arg->hash)->tbl;
    struct st_table_entry **bins = tbl->bins;

    if (key == Qundef) return ST_CONTINUE;
    status = (*arg->func)(key, value, arg->arg);
    if (RHASH(arg->hash)->tbl != tbl || RHASH(arg->hash)->tbl->bins != bins){
	rb_raise(rb_eIndexError, "rehash occurred during iteration");
    }
    return status;
}

static VALUE
rb_hash_foreach_call(arg)
    struct rb_hash_foreach_arg *arg;
{
    st_foreach(RHASH(arg->hash)->tbl, rb_hash_foreach_iter, (st_data_t)arg);
    return Qnil;
}

static VALUE
rb_hash_foreach_ensure(hash)
    VALUE hash;
{
    RHASH(hash)->iter_lev--;

    if (RHASH(hash)->iter_lev == 0) {
	if (FL_TEST(hash, HASH_DELETED)) {
	    st_cleanup_safe(RHASH(hash)->tbl, Qundef);
	    FL_UNSET(hash, HASH_DELETED);
	}
    }
    return 0;
}

static int
rb_hash_foreach(hash, func, farg)
    VALUE hash;
    enum st_retval (*func)();
    VALUE farg;
{
    struct rb_hash_foreach_arg arg;

    RHASH(hash)->iter_lev++;
    arg.hash = hash;
    arg.func = func;
    arg.arg  = farg;
    return rb_ensure(rb_hash_foreach_call, (VALUE)&arg, rb_hash_foreach_ensure, hash);
}

static VALUE hash_alloc _((VALUE));
static VALUE
hash_alloc(klass)
    VALUE klass;
{
    NEWOBJ(hash, struct RHash);
    OBJSETUP(hash, klass, T_HASH);

    hash->ifnone = Qnil;
    hash->tbl = st_init_table(&objhash);

    return (VALUE)hash;
}

VALUE
rb_hash_new()
{
    return hash_alloc(rb_cHash);
}

static VALUE
rb_hash_initialize(argc, argv, hash)
    int argc;
    VALUE *argv;
    VALUE hash;
{
    VALUE ifnone;

    rb_hash_modify(hash);
    if (rb_block_given_p()) {
	if (argc > 0) {
	    rb_raise(rb_eArgError, "wrong number of arguments");
	}
	RHASH(hash)->ifnone = rb_block_proc();
	FL_SET(hash, HASH_PROC_DEFAULT);
    }
    else {
	rb_scan_args(argc, argv, "01", &ifnone);
	RHASH(hash)->ifnone = ifnone;
    }

    return hash;
}

static VALUE
rb_hash_s_create(argc, argv, klass)
    int argc;
    VALUE *argv;
    VALUE klass;
{
    VALUE hash;
    int i;

    if (argc == 1 && TYPE(argv[0]) == T_HASH) {
	hash = hash_alloc(klass);
	    
	RHASH(hash)->ifnone = Qnil;
	RHASH(hash)->tbl = st_copy(RHASH(argv[0])->tbl);

	return hash;
    }

    if (argc % 2 != 0) {
	rb_raise(rb_eArgError, "odd number args for Hash");
    }

    hash = hash_alloc(klass);
    for (i=0; i<argc; i+=2) {
        rb_hash_aset(hash, argv[i], argv[i + 1]);
    }

    return hash;
}

static VALUE
to_hash(hash)
    VALUE hash;
{
    return rb_convert_type(hash, T_HASH, "Hash", "to_hash");
}

static int
rb_hash_rehash_i(key, value, tbl)
    VALUE key, value;
    st_table *tbl;
{
    if (key != Qundef) st_insert(tbl, key, value);
    return ST_CONTINUE;
}

static VALUE
rb_hash_rehash(hash)
    VALUE hash;
{
    st_table *tbl;

    rb_hash_modify(hash);
    tbl = st_init_table_with_size(&objhash, RHASH(hash)->tbl->num_entries);
    st_foreach(RHASH(hash)->tbl, rb_hash_rehash_i, (st_data_t)tbl);
    st_free_table(RHASH(hash)->tbl);
    RHASH(hash)->tbl = tbl;

    return hash;
}

VALUE
rb_hash_aref(hash, key)
    VALUE hash, key;
{
    VALUE val;

    if (!st_lookup(RHASH(hash)->tbl, key, &val)) {
	return rb_funcall(hash, id_default, 1, key);
    }
    return val;
}

static VALUE
rb_hash_fetch(argc, argv, hash)
    int argc;
    VALUE *argv;
    VALUE hash;
{
    VALUE key, if_none;
    VALUE val;

    rb_scan_args(argc, argv, "11", &key, &if_none);

    if (!st_lookup(RHASH(hash)->tbl, key, &val)) {
	if (rb_block_given_p()) {
	    if (argc > 1) {
               rb_raise(rb_eArgError, "wrong number of arguments");
	    }
	    return rb_yield(key);
	}
	if (argc == 1) {
	    rb_raise(rb_eIndexError, "key not found");
	}
	return if_none;
    }
    return val;
}

static VALUE
rb_hash_default(argc, argv, hash)
    int argc;
    VALUE *argv;
    VALUE hash;
{
    VALUE key;

    rb_scan_args(argc, argv, "01", &key);
    if (FL_TEST(hash, HASH_PROC_DEFAULT)) {
	return rb_funcall(RHASH(hash)->ifnone, id_call, 2, hash, key);
    }
    return RHASH(hash)->ifnone;
}

static VALUE
rb_hash_set_default(hash, ifnone)
    VALUE hash, ifnone;
{
    rb_hash_modify(hash);
    RHASH(hash)->ifnone = ifnone;
    FL_UNSET(hash, HASH_PROC_DEFAULT);
    return ifnone;
}

static VALUE
rb_hash_default_proc(hash)
    VALUE hash;
{
    if (FL_TEST(hash, HASH_PROC_DEFAULT)) {
	return RHASH(hash)->ifnone;
    }
    return Qnil;
}

static int
index_i(key, value, args)
    VALUE key, value;
    VALUE *args;
{
    if (rb_equal(value, args[0])) {
	args[1] = key;
	return ST_STOP;
    }
    return ST_CONTINUE;
}

static VALUE
rb_hash_index(hash, value)
    VALUE hash, value;
{
    VALUE args[2];

    args[0] = value;
    args[1] = Qnil;

    st_foreach(RHASH(hash)->tbl, index_i, (st_data_t)args);

    return args[1];
}

static VALUE
rb_hash_indexes(argc, argv, hash)
    int argc;
    VALUE *argv;
    VALUE hash;
{
    VALUE indexes;
    int i;

    rb_warn("Hash#%s is deprecated; use Hash#values_at",
	    rb_id2name(rb_frame_last_func()));
    indexes = rb_ary_new2(argc);
    for (i=0; i<argc; i++) {
	RARRAY(indexes)->ptr[i] = rb_hash_aref(hash, argv[i]);
	RARRAY(indexes)->len++;
    }
    return indexes;
}

VALUE
rb_hash_delete(hash, key)
    VALUE hash, key;
{
    VALUE val;

    rb_hash_modify(hash);
    if (RHASH(hash)->iter_lev > 0) {
	if (st_delete_safe(RHASH(hash)->tbl, (st_data_t*)&key, &val, Qundef)) {
	    FL_SET(hash, HASH_DELETED);
	    return val;
	}
    }
    else if (st_delete(RHASH(hash)->tbl, (st_data_t*)&key, &val))
	return val;
    if (rb_block_given_p()) {
	return rb_yield(key);
    }
    return Qnil;
}

struct shift_var {
    int stop;
    VALUE key;
    VALUE val;
};

static int
shift_i(key, value, var)
    VALUE key, value;
    struct shift_var *var;
{
    if (key == Qundef) return ST_CONTINUE;
    if (var->stop) return ST_STOP;
    var->stop = 1;
    var->key = key;
    var->val = value;
    return ST_DELETE;
}

static VALUE
rb_hash_shift(hash)
    VALUE hash;
{
    struct shift_var var;

    rb_hash_modify(hash);
    var.stop = 0;
    st_foreach(RHASH(hash)->tbl, shift_i, (st_data_t)&var);

    if (var.stop) {
	return rb_assoc_new(var.key, var.val);
    }
    else if (FL_TEST(hash, HASH_PROC_DEFAULT)) {
	return rb_funcall(RHASH(hash)->ifnone, id_call, 2, hash, Qnil);
    }
    else {
	return RHASH(hash)->ifnone;
    }
}

static enum st_retval
delete_if_i(key, value)
    VALUE key, value;
{
    if (key == Qundef) return ST_CONTINUE;
    if (RTEST(rb_yield_values(2, key, value)))
	return ST_DELETE;
    return ST_CONTINUE;
}

VALUE
rb_hash_delete_if(hash)
    VALUE hash;
{
    rb_hash_modify(hash);
    rb_hash_foreach(hash, delete_if_i, 0);
    return hash;
}

VALUE
rb_hash_reject_bang(hash)
    VALUE hash;
{
    int n = RHASH(hash)->tbl->num_entries;
    rb_hash_delete_if(hash);
    if (n == RHASH(hash)->tbl->num_entries) return Qnil;
    return hash;
}

static VALUE
rb_hash_reject(hash)
    VALUE hash;
{
    return rb_hash_delete_if(rb_obj_dup(hash));
}

static enum st_retval
select_i(key, value, result)
    VALUE key, value, result;
{
    if (key == Qundef) return ST_CONTINUE;
    if (RTEST(rb_yield_values(2, key, value)))
	rb_ary_push(result, rb_assoc_new(key, value));
    return ST_CONTINUE;
}

VALUE
rb_hash_values_at(argc, argv, hash)
    int argc;
    VALUE *argv;
    VALUE hash;
{
    VALUE result = rb_ary_new();
    long i;

    for (i=0; i<argc; i++) {
	rb_ary_push(result, rb_hash_aref(hash, argv[i]));
    }
    return result;
}

VALUE
rb_hash_select(argc, argv, hash)
    int argc;
    VALUE *argv;
    VALUE hash;
{
    VALUE result;

    if (!rb_block_given_p()) {
#if RUBY_VERSION_CODE < 181
	rb_warn("Hash#select(key..) is deprecated; use Hash#values_at");
#endif
	return rb_hash_values_at(argc, argv, hash);
    }
    if (argc > 0) {
	rb_raise(rb_eArgError, "wrong number arguments(%d for 0)", argc);
    }
    result = rb_ary_new();
    rb_hash_foreach(hash, select_i, result);
    return result;
}

static int
clear_i(key, value, dummy)
    VALUE key, value, dummy;
{
    return ST_DELETE;
}

static VALUE
rb_hash_clear(hash)
    VALUE hash;
{
    rb_hash_modify(hash);
    st_foreach(RHASH(hash)->tbl, clear_i, 0);

    return hash;
}

VALUE
rb_hash_aset(hash, key, val)
    VALUE hash, key, val;
{
    rb_hash_modify(hash);
    if (TYPE(key) != T_STRING || st_lookup(RHASH(hash)->tbl, key, 0)) {
	st_insert(RHASH(hash)->tbl, key, val);
    }
    else {
	st_add_direct(RHASH(hash)->tbl, rb_str_new4(key), val);
    }
    return val;
}

static int
replace_i(key, val, hash)
    VALUE key, val, hash;
{
    if (key != Qundef) {
	rb_hash_aset(hash, key, val);
    }

    return ST_CONTINUE;
}

static VALUE
rb_hash_replace(hash, hash2)
    VALUE hash, hash2;
{
    hash2 = to_hash(hash2);
    if (hash == hash2) return hash;
    rb_hash_clear(hash);
    st_foreach(RHASH(hash2)->tbl, replace_i, hash);
    RHASH(hash)->ifnone = RHASH(hash2)->ifnone;
    if (FL_TEST(hash2, HASH_PROC_DEFAULT)) {
	FL_SET(hash, HASH_PROC_DEFAULT);
    }
    else {
	FL_UNSET(hash, HASH_PROC_DEFAULT);
    }

    return hash;
}

static VALUE
rb_hash_size(hash)
    VALUE hash;
{
    return INT2FIX(RHASH(hash)->tbl->num_entries);
}

static VALUE
rb_hash_empty_p(hash)
    VALUE hash;
{
    if (RHASH(hash)->tbl->num_entries == 0)
	return Qtrue;
    return Qfalse;
}

static enum st_retval
each_value_i(key, value)
    VALUE key, value;
{
    if (key == Qundef) return ST_CONTINUE;
    rb_yield(value);
    return ST_CONTINUE;
}

static VALUE
rb_hash_each_value(hash)
    VALUE hash;
{
    rb_hash_foreach(hash, each_value_i, 0);
    return hash;
}

static enum st_retval
each_key_i(key, value)
    VALUE key, value;
{
    if (key == Qundef) return ST_CONTINUE;
    rb_yield(key);
    return ST_CONTINUE;
}

static VALUE
rb_hash_each_key(hash)
    VALUE hash;
{
    rb_hash_foreach(hash, each_key_i, 0);
    return hash;
}

static enum st_retval
each_pair_i(key, value)
    VALUE key, value;
{
    if (key == Qundef) return ST_CONTINUE;
    rb_yield_values(2, key, value);
    return ST_CONTINUE;
}

static VALUE
rb_hash_each_pair(hash)
    VALUE hash;
{
    rb_hash_foreach(hash, each_pair_i, 0);
    return hash;
}

static int
to_a_i(key, value, ary)
    VALUE key, value, ary;
{
    if (key == Qundef) return ST_CONTINUE;
    rb_ary_push(ary, rb_assoc_new(key, value));
    return ST_CONTINUE;
}

static VALUE
rb_hash_to_a(hash)
    VALUE hash;
{
    VALUE ary;

    ary = rb_ary_new();
    st_foreach(RHASH(hash)->tbl, to_a_i, ary);
    if (OBJ_TAINTED(hash)) OBJ_TAINT(ary);

    return ary;
}

static VALUE
rb_hash_sort(hash)
    VALUE hash;
{
    VALUE entries = rb_hash_to_a(hash);
    rb_ary_sort_bang(entries);
    return entries;
}

static int
inspect_i(key, value, str)
    VALUE key, value, str;
{
    VALUE str2;

    if (key == Qundef) return ST_CONTINUE;
    if (RSTRING(str)->len > 1) {
	rb_str_cat2(str, ", ");
    }
    str2 = rb_inspect(key);
    rb_str_buf_append(str, str2);
    OBJ_INFECT(str, str2);
    rb_str_buf_cat2(str, "=>");
    str2 = rb_inspect(value);
    rb_str_buf_append(str, str2);
    OBJ_INFECT(str, str2);

    return ST_CONTINUE;
}

static VALUE
inspect_hash(hash)
    VALUE hash;
{
    VALUE str;

    str = rb_str_buf_new2("{");
    st_foreach(RHASH(hash)->tbl, inspect_i, str);
    rb_str_buf_cat2(str, "}");
    OBJ_INFECT(str, hash);

    return str;
}

static VALUE
rb_hash_inspect(hash)
    VALUE hash;
{
    if (RHASH(hash)->tbl == 0 || RHASH(hash)->tbl->num_entries == 0)
	return rb_str_new2("{}");
    if (rb_inspecting_p(hash)) return rb_str_new2("{...}");
    return rb_protect_inspect(inspect_hash, hash, 0);
}

static VALUE
to_s_hash(hash)
    VALUE hash;
{
    return rb_ary_to_s(rb_hash_to_a(hash));
}

static VALUE
rb_hash_to_s(hash)
    VALUE hash;
{
    if (rb_inspecting_p(hash)) return rb_str_new2("{...}");
    return rb_protect_inspect(to_s_hash, hash, 0);
}

static VALUE
rb_hash_to_hash(hash)
    VALUE hash;
{
    return hash;
}

static int
keys_i(key, value, ary)
    VALUE key, value, ary;
{
    if (key == Qundef) return ST_CONTINUE;
    rb_ary_push(ary, key);
    return ST_CONTINUE;
}

static VALUE
rb_hash_keys(hash)
    VALUE hash;
{
    VALUE ary;

    ary = rb_ary_new();
    st_foreach(RHASH(hash)->tbl, keys_i, ary);

    return ary;
}

static int
values_i(key, value, ary)
    VALUE key, value, ary;
{
    if (key == Qundef) return ST_CONTINUE;
    rb_ary_push(ary, value);
    return ST_CONTINUE;
}

static VALUE
rb_hash_values(hash)
    VALUE hash;
{
    VALUE ary;

    ary = rb_ary_new();
    st_foreach(RHASH(hash)->tbl, values_i, ary);

    return ary;
}

static VALUE
rb_hash_has_key(hash, key)
    VALUE hash;
    VALUE key;
{
    if (st_lookup(RHASH(hash)->tbl, key, 0)) {
	return Qtrue;
    }
    return Qfalse;
}

static int
rb_hash_search_value(key, value, data)
    VALUE key, value, *data;
{
    if (key == Qundef) return ST_CONTINUE;
    if (rb_equal(value, data[1])) {
	data[0] = Qtrue;
	return ST_STOP;
    }
    return ST_CONTINUE;
}

static VALUE
rb_hash_has_value(hash, val)
    VALUE hash;
    VALUE val;
{
    VALUE data[2];

    data[0] = Qfalse;
    data[1] = val;
    st_foreach(RHASH(hash)->tbl, rb_hash_search_value, (st_data_t)data);
    return data[0];
}

struct equal_data {
    int result;
    st_table *tbl;
};

static int
equal_i(key, val1, data)
    VALUE key, val1;
    struct equal_data *data;
{
    VALUE val2;

    if (key == Qundef) return ST_CONTINUE;
    if (!st_lookup(data->tbl, key, &val2)) {
	data->result = Qfalse;
	return ST_STOP;
    }
    if (!rb_equal(val1, val2)) {
	data->result = Qfalse;
	return ST_STOP;
    }
    return ST_CONTINUE;
}

static VALUE
rb_hash_equal(hash1, hash2)
    VALUE hash1, hash2;
{
    struct equal_data data;

    if (hash1 == hash2) return Qtrue;
    if (TYPE(hash2) != T_HASH) {
	if (!rb_respond_to(hash2, rb_intern("to_hash"))) {
	    return Qfalse;
	}
	return rb_equal(hash2, hash1);
    }
    if (RHASH(hash1)->tbl->num_entries != RHASH(hash2)->tbl->num_entries)
	return Qfalse;
    if (!(rb_equal(RHASH(hash1)->ifnone, RHASH(hash2)->ifnone) &&
	  FL_TEST(hash1, HASH_PROC_DEFAULT) == FL_TEST(hash2, HASH_PROC_DEFAULT)))
	return Qfalse;

    data.tbl = RHASH(hash2)->tbl;
    data.result = Qtrue;
    st_foreach(RHASH(hash1)->tbl, equal_i, (st_data_t)&data);

    return data.result;
}

static int
rb_hash_invert_i(key, value, hash)
    VALUE key, value;
    VALUE hash;
{
    if (key == Qundef) return ST_CONTINUE;
    rb_hash_aset(hash, value, key);
    return ST_CONTINUE;
}

static VALUE
rb_hash_invert(hash)
    VALUE hash;
{
    VALUE h = rb_hash_new();

    st_foreach(RHASH(hash)->tbl, rb_hash_invert_i, h);
    return h;
}

static int
rb_hash_update_i(key, value, hash)
    VALUE key, value;
    VALUE hash;
{
    if (key == Qundef) return ST_CONTINUE;
    rb_hash_aset(hash, key, value);
    return ST_CONTINUE;
}

static int
rb_hash_update_block_i(key, value, hash)
    VALUE key, value;
    VALUE hash;
{
    if (key == Qundef) return ST_CONTINUE;
    if (rb_hash_has_key(hash, key)) {
	value = rb_yield_values(3, key, rb_hash_aref(hash, key), value);
    }
    rb_hash_aset(hash, key, value);
    return ST_CONTINUE;
}

static VALUE
rb_hash_update(hash1, hash2)
    VALUE hash1, hash2;
{
    hash2 = to_hash(hash2);
    if (rb_block_given_p()) {
	st_foreach(RHASH(hash2)->tbl, rb_hash_update_block_i, hash1);
    }
    else {
	st_foreach(RHASH(hash2)->tbl, rb_hash_update_i, hash1);
    }
    return hash1;
}

static VALUE
rb_hash_merge(hash1, hash2)
    VALUE hash1, hash2;
{
    return rb_hash_update(rb_obj_dup(hash1), hash2);
}

static int path_tainted = -1;

static char **origenviron;
#ifdef _WIN32
#define GET_ENVIRON(e) (e = rb_w32_get_environ())
#define FREE_ENVIRON(e) rb_w32_free_environ(e)
static char **my_environ;
#undef environ
#define environ my_environ
#elif defined(__APPLE__)
#undef environ
#define environ (*_NSGetEnviron())
#define GET_ENVIRON(e) (e)
#define FREE_ENVIRON(e)
#else
extern char **environ;
#define GET_ENVIRON(e) (e)
#define FREE_ENVIRON(e)
#endif

static VALUE
env_str_new(ptr, len)
    const char *ptr;
    long len;
{
    VALUE str = rb_tainted_str_new(ptr, len);

    rb_obj_freeze(str);
    return str;
}

static VALUE
env_str_new2(ptr)
    const char *ptr;
{
    if (!ptr) return Qnil;
    return env_str_new(ptr, strlen(ptr));
}

static VALUE
env_delete(obj, name)
    VALUE obj, name;
{
    char *nam, *val;

    rb_secure(4);
    SafeStringValue(name);
    nam = RSTRING(name)->ptr;
    if (strlen(nam) != RSTRING(name)->len) {
	rb_raise(rb_eArgError, "bad environment variable name");
    }
    val = getenv(nam);
    if (val) {
	VALUE value = env_str_new2(val);

	ruby_setenv(nam, 0);
#ifdef ENV_IGNORECASE
	if (strcasecmp(nam, PATH_ENV) == 0)
#else
	if (strcmp(nam, PATH_ENV) == 0)
#endif
	{
	    path_tainted = 0;
	}
	return value;
    }
    return Qnil;
}

static VALUE
env_delete_m(obj, name)
    VALUE obj, name;
{
    VALUE val;

    val = env_delete(obj, name);
    if (NIL_P(val) && rb_block_given_p()) rb_yield(name);
    return val;
}

static VALUE
rb_f_getenv(obj, name)
    VALUE obj, name;
{
    char *nam, *env;

    StringValue(name);
    nam = RSTRING(name)->ptr;
    if (strlen(nam) != RSTRING(name)->len) {
	rb_raise(rb_eArgError, "bad environment variable name");
    }
    env = getenv(nam);
    if (env) {
#ifdef ENV_IGNORECASE
	if (strcasecmp(nam, PATH_ENV) == 0 && !rb_env_path_tainted())
#else
	if (strcmp(nam, PATH_ENV) == 0 && !rb_env_path_tainted())
#endif
	{
	    VALUE str = rb_str_new2(env);

	    rb_obj_freeze(str);
	    return str;
	}
	return env_str_new2(env);
    }
    return Qnil;
}

static VALUE
env_fetch(argc, argv)
    int argc;
    VALUE *argv;
{
    VALUE key, if_none;
    char *nam, *env;

    rb_scan_args(argc, argv, "11", &key, &if_none);
    StringValue(key);
    nam = RSTRING(key)->ptr;
    if (strlen(nam) != RSTRING(key)->len) {
	rb_raise(rb_eArgError, "bad environment variable name");
    }
    env = getenv(nam);
    if (!env) {
	if (rb_block_given_p()) {
	    if (argc > 1) {
		rb_raise(rb_eArgError, "wrong number of arguments");
	    }
	    return rb_yield(key);
	}
	if (argc == 1) {
	    rb_raise(rb_eIndexError, "key not found");
	}
	return if_none;
    }
#ifdef ENV_IGNORECASE
    if (strcasecmp(nam, PATH_ENV) == 0 && !rb_env_path_tainted())
#else
    if (strcmp(nam, PATH_ENV) == 0 && !rb_env_path_tainted())
#endif
	return rb_str_new2(env);
    return env_str_new2(env);
}

static void
path_tainted_p(path)
    char *path;
{
    path_tainted = rb_path_check(path)?0:1;
}

int
rb_env_path_tainted()
{
    if (path_tainted < 0) {
	path_tainted_p(getenv(PATH_ENV));
    }
    return path_tainted;
}

static int
envix(nam)
    const char *nam;
{
    register int i, len = strlen(nam);
    char **env;

    env = GET_ENVIRON(environ);
    for (i = 0; env[i]; i++) {
	if (
#ifdef ENV_IGNORECASE
	    strncasecmp(env[i],nam,len) == 0
#else
	    memcmp(env[i],nam,len) == 0
#endif
	    && env[i][len] == '=')
	    break;			/* memcmp must come first to avoid */
    }					/* potential SEGV's */
    FREE_ENVIRON(environ);
    return i;
}

void
ruby_setenv(name, value)
    const char *name;
    const char *value;
{
#if defined(_WIN32)
    /* The sane way to deal with the environment.
     * Has these advantages over putenv() & co.:
     *  * enables us to store a truly empty value in the
     *    environment (like in UNIX).
     *  * we don't have to deal with RTL globals, bugs and leaks.
     *  * Much faster.
     * Why you may want to enable USE_WIN32_RTL_ENV:
     *  * environ[] and RTL functions will not reflect changes,
     *    which might be an issue if extensions want to access
     *    the env. via RTL.  This cuts both ways, since RTL will
     *    not see changes made by extensions that call the Win32
     *    functions directly, either.
     * GSAR 97-06-07
     *
     * REMARK: USE_WIN32_RTL_ENV is already obsoleted since we don't use
     *         RTL's environ global variable directly yet.
     */
    SetEnvironmentVariable(name,value);
#elif defined __CYGWIN__
#undef setenv
#undef unsetenv
    if (value)
	setenv(name,value,1);
    else
	unsetenv(name);
#else  /* WIN32 */

    int i=envix(name);		        /* where does it go? */

    if (environ == origenviron) {	/* need we copy environment? */
	int j;
	int max;
	char **tmpenv;

	for (max = i; environ[max]; max++) ;
	tmpenv = ALLOC_N(char*, max+2);
	for (j=0; j<max; j++)		/* copy environment */
	    tmpenv[j] = strdup(environ[j]);
	tmpenv[max] = 0;
	environ = tmpenv;		/* tell exec where it is now */
    }
    if (!value) {
	if (environ != origenviron) {
	    char **envp = origenviron;
	    while (*envp && *envp != environ[i]) envp++;
	    if (!*envp)
		free(environ[i]);
	}
	while (environ[i]) {
	    environ[i] = environ[i+1];
	    i++;
	}
	return;
    }
    if (!environ[i]) {			/* does not exist yet */
	REALLOC_N(environ, char*, i+2);	/* just expand it a bit */
	environ[i+1] = 0;	/* make sure it's null terminated */
    }
    else {
	if (environ[i] != origenviron[i])
	    free(environ[i]);
    }
    environ[i] = ALLOC_N(char, strlen(name) + strlen(value) + 2);
#ifndef MSDOS
    sprintf(environ[i],"%s=%s",name,value); /* all that work just for this */
#else
    /* MS-DOS requires environment variable names to be in uppercase */
    /* [Tom Dinger, 27 August 1990: Well, it doesn't _require_ it, but
     * some utilities and applications may break because they only look
     * for upper case strings. (Fixed strupr() bug here.)]
     */
    strcpy(environ[i],name); strupr(environ[i]);
    sprintf(environ[i] + strlen(name),"=%s", value);
#endif /* MSDOS */

#endif /* WIN32 */
}

void
ruby_unsetenv(name)
    const char *name;
{
    ruby_setenv(name, 0);
}

static VALUE
env_aset(obj, nm, val)
    VALUE obj, nm, val;
{
    char *name, *value;

    if (rb_safe_level() >= 4) {
	rb_raise(rb_eSecurityError, "cannot change environment variable");
    }

    if (NIL_P(val)) {
	env_delete(obj, nm);
	return Qnil;
    }

    StringValue(nm);
    StringValue(val);
    name = RSTRING(nm)->ptr;
    value = RSTRING(val)->ptr;
    if (strlen(name) != RSTRING(nm)->len)
	rb_raise(rb_eArgError, "bad environment variable name");
    if (strlen(value) != RSTRING(val)->len)
	rb_raise(rb_eArgError, "bad environment variable value");

    ruby_setenv(name, value);
#ifdef ENV_IGNORECASE
    if (strcasecmp(name, PATH_ENV) == 0) {
#else
    if (strcmp(name, PATH_ENV) == 0) {
#endif
	if (OBJ_TAINTED(val)) {
	    /* already tainted, no check */
	    path_tainted = 1;
	    return val;
	}
	else {
	    path_tainted_p(value);
	}
    }
    return val;
}

static VALUE
env_keys()
{
    char **env;
    VALUE ary = rb_ary_new();

    env = GET_ENVIRON(environ);
    while (*env) {
	char *s = strchr(*env, '=');
	if (s) {
	    rb_ary_push(ary, env_str_new(*env, s-*env));
	}
	env++;
    }
    FREE_ENVIRON(environ);
    return ary;
}

static VALUE
env_each_key(ehash)
    VALUE ehash;
{
    VALUE keys = env_keys();
    long i;

    for (i=0; i<RARRAY(keys)->len; i++) {
	rb_yield(RARRAY(keys)->ptr[i]);
    }
    return ehash;
}

static VALUE
env_values()
{
    char **env;
    VALUE ary = rb_ary_new();

    env = GET_ENVIRON(environ);
    while (*env) {
	char *s = strchr(*env, '=');
	if (s) {
	    rb_ary_push(ary, env_str_new2(s+1));
	}
	env++;
    }
    FREE_ENVIRON(environ);
    return ary;
}

static VALUE
env_each_value(ehash)
    VALUE ehash;
{
    VALUE values = env_values();
    long i;

    for (i=0; i<RARRAY(values)->len; i++) {
	rb_yield(RARRAY(values)->ptr[i]);
    }
    return ehash;
}

static VALUE
env_each(ehash)
    VALUE ehash;
{
    char **env;
    VALUE ary = rb_ary_new();
    long i;

    env = GET_ENVIRON(environ);
    while (*env) {
	char *s = strchr(*env, '=');
	if (s) {
	    rb_ary_push(ary, env_str_new(*env, s-*env));
	    rb_ary_push(ary, env_str_new2(s+1));
	}
	env++;
    }
    FREE_ENVIRON(environ);

    for (i=0; i<RARRAY(ary)->len; i+=2) {
	rb_yield_values(2, RARRAY(ary)->ptr[i], RARRAY(ary)->ptr[i+1]);
    }
    return ehash;
}

static VALUE
env_reject_bang()
{
    volatile VALUE keys;
    long i;
    int del = 0;

    rb_secure(4);
    keys = env_keys();

    for (i=0; i<RARRAY(keys)->len; i++) {
	VALUE val = rb_f_getenv(Qnil, RARRAY(keys)->ptr[i]);
	if (!NIL_P(val)) {
	    if (RTEST(rb_yield_values(2, RARRAY(keys)->ptr[i], val))) {
		FL_UNSET(RARRAY(keys)->ptr[i], FL_TAINT);
		env_delete(Qnil, RARRAY(keys)->ptr[i]);
		del++;
	    }
	}
    }
    if (del == 0) return Qnil;
    return envtbl;
}

static VALUE
env_delete_if()
{
    env_reject_bang();
    return envtbl;
}

static VALUE
env_values_at(argc, argv)
    int argc;
    VALUE *argv;
{
    VALUE result = rb_ary_new();
    long i;

    for (i=0; i<argc; i++) {
	rb_ary_push(result, rb_f_getenv(Qnil, argv[i]));
    }
    return result;
}

static VALUE
env_select(argc, argv)
    int argc;
    VALUE *argv;
{
    VALUE result;
    char **env;

    if (!rb_block_given_p()) {
	rb_warn("ENV.select(index..) is deprecated; use ENV.values_at");
	return env_values_at(argc, argv);
    }
    if (argc > 0) {
	rb_raise(rb_eArgError, "wrong number arguments(%d for 0)", argc);
    }
    result = rb_ary_new();
    env = GET_ENVIRON(environ);
    while (*env) {
	char *s = strchr(*env, '=');
	if (s) {
	    VALUE k = env_str_new(*env, s-*env);
	    VALUE v = env_str_new2(s+1);
	    if (RTEST(rb_yield_values(2, k, v))) {
		rb_ary_push(result, rb_assoc_new(k, v));
	    }
	}
	env++;
    }
    FREE_ENVIRON(environ);

    return result;
}

static VALUE
env_clear()
{
    volatile VALUE keys;
    long i;
    
    rb_secure(4);
    keys = env_keys();

    for (i=0; i<RARRAY(keys)->len; i++) {
	VALUE val = rb_f_getenv(Qnil, RARRAY(keys)->ptr[i]);
	if (!NIL_P(val)) {
	    env_delete(Qnil, RARRAY(keys)->ptr[i]);
	}
    }
    return envtbl;
}

static VALUE
env_to_s()
{
    return rb_str_new2("ENV");
}

static VALUE
env_inspect()
{
    char **env;
    VALUE str = rb_str_buf_new2("{");
    VALUE i;

    env = GET_ENVIRON(environ);
    while (*env) {
	char *s = strchr(*env, '=');

	if (env != environ) {
	    rb_str_buf_cat2(str, ", ");
	}
	if (s) {
	    rb_str_buf_cat2(str, "\"");
	    rb_str_buf_cat(str, *env, s-*env);
	    rb_str_buf_cat2(str, "\"=>");
	    i = rb_inspect(rb_str_new2(s+1));
	    rb_str_buf_append(str, i);
	}
	env++;
    }
    FREE_ENVIRON(environ);
    rb_str_buf_cat2(str, "}");
    OBJ_TAINT(str);

    return str;
}

static VALUE
env_to_a()
{
    char **env;
    VALUE ary = rb_ary_new();

    env = GET_ENVIRON(environ);
    while (*env) {
	char *s = strchr(*env, '=');
	if (s) {
	    rb_ary_push(ary, rb_assoc_new(env_str_new(*env, s-*env),
					  env_str_new2(s+1)));
	}
	env++;
    }
    FREE_ENVIRON(environ);
    return ary;
}

static VALUE
env_none()
{
    return Qnil;
}

static VALUE
env_size()
{
    int i;
    char **env;

    env = GET_ENVIRON(environ);
    for(i=0; env[i]; i++)
	;
    FREE_ENVIRON(environ);
    return INT2FIX(i);
}

static VALUE
env_empty_p()
{
    char **env;

    env = GET_ENVIRON(environ);
    if (env[0] == 0) {
	FREE_ENVIRON(environ);
	return Qtrue;
    }
    FREE_ENVIRON(environ);
    return Qfalse;
}

static VALUE
env_has_key(env, key)
    VALUE env, key;
{
    char *s;

    s = StringValuePtr(key);
    if (strlen(s) != RSTRING(key)->len)
	rb_raise(rb_eArgError, "bad environment variable name");
    if (getenv(s)) return Qtrue;
    return Qfalse;
}

static VALUE
env_has_value(dmy, value)
    VALUE dmy, value;
{
    char **env;

    if (TYPE(value) != T_STRING) return Qfalse;
    env = GET_ENVIRON(environ);
    while (*env) {
	char *s = strchr(*env, '=');
	if (s++) {
#ifdef ENV_IGNORECASE
	    if (strncasecmp(s, RSTRING(value)->ptr, strlen(s)) == 0) {
#else
	    if (strncmp(s, RSTRING(value)->ptr, strlen(s)) == 0) {
#endif
		FREE_ENVIRON(environ);
		return Qtrue;
	    }
	}
	env++;
    }
    FREE_ENVIRON(environ);
    return Qfalse;
}

static VALUE
env_index(dmy, value)
    VALUE dmy, value;
{
    char **env;
    VALUE str;

    StringValue(value);
    env = GET_ENVIRON(environ);
    while (*env) {
	char *s = strchr(*env, '=');
	if (s++) {
#ifdef ENV_IGNORECASE
	    if (strncasecmp(s, RSTRING(value)->ptr, strlen(s)) == 0) {
#else
	    if (strncmp(s, RSTRING(value)->ptr, strlen(s)) == 0) {
#endif
		str = env_str_new(*env, s-*env-1);
		FREE_ENVIRON(environ);
		return str;
	    }
	}
	env++;
    }
    FREE_ENVIRON(environ);
    return Qnil;
}

static VALUE
env_indexes(argc, argv)
    int argc;
    VALUE *argv;
{
    int i;
    VALUE indexes = rb_ary_new2(argc);

    rb_warn("ENV.%s is deprecated; use ENV.values_at",
	    rb_id2name(rb_frame_last_func()));
    for (i=0;i<argc;i++) {
	VALUE tmp = rb_check_string_type(argv[i]);
	if (NIL_P(tmp)) {
	    RARRAY(indexes)->ptr[i] = Qnil;
	}
	else {
	    RARRAY(indexes)->ptr[i] = env_str_new2(getenv(RSTRING(tmp)->ptr));
	}
	RARRAY(indexes)->len = i+1;
    }

    return indexes;
}

static VALUE
env_to_hash()
{
    char **env;
    VALUE hash = rb_hash_new();

    env = GET_ENVIRON(environ);
    while (*env) {
	char *s = strchr(*env, '=');
	if (s) {
	    rb_hash_aset(hash, env_str_new(*env, s-*env),
			       env_str_new2(s+1));
	}
	env++;
    }
    FREE_ENVIRON(environ);
    return hash;
}

static VALUE
env_reject()
{
    return rb_hash_delete_if(env_to_hash());
}

static VALUE
env_shift()
{
    char **env;

    env = GET_ENVIRON(environ);
    if (*env) {
	char *s = strchr(*env, '=');
	if (s) {
	    VALUE key = env_str_new(*env, s-*env);
	    VALUE val = env_str_new2(getenv(RSTRING(key)->ptr));
	    env_delete(Qnil, key);
	    return rb_assoc_new(key, val);
	}
    }
    FREE_ENVIRON(environ);
    return Qnil;
}

static VALUE
env_invert()
{
    return rb_hash_invert(env_to_hash());
}

static int
env_replace_i(key, val, keys)
    VALUE key, val, keys;
{
    if (key != Qundef) {
	env_aset(Qnil, key, val);
	if (rb_ary_includes(keys, key)) {
	    rb_ary_delete(keys, key);
	}
    }
    return ST_CONTINUE;
}

static VALUE
env_replace(env, hash)
    VALUE env, hash;
{
    volatile VALUE keys = env_keys();
    long i;

    if (env == hash) return env;
    hash = to_hash(hash);
    st_foreach(RHASH(hash)->tbl, env_replace_i, keys);

    for (i=0; i<RARRAY(keys)->len; i++) {
	env_delete(env, RARRAY(keys)->ptr[i]);
    }
    return env;
}

static int
env_update_i(key, val)
    VALUE key, val;
{
    if (key != Qundef) {
	if (rb_block_given_p()) {
	    val = rb_yield_values(3, key, rb_f_getenv(Qnil, key), val);
	}
	env_aset(Qnil, key, val);
    }
    return ST_CONTINUE;
}

static VALUE
env_update(env, hash)
    VALUE env, hash;
{
    if (env == hash) return env;
    hash = to_hash(hash);
    st_foreach(RHASH(hash)->tbl, env_update_i, 0);
    return env;
}

void
Init_Hash()
{
    id_hash = rb_intern("hash");
    id_call = rb_intern("call");
    id_default = rb_intern("default");

    rb_cHash = rb_define_class("Hash", rb_cObject);

    rb_include_module(rb_cHash, rb_mEnumerable);

    rb_define_alloc_func(rb_cHash, hash_alloc);
    rb_define_singleton_method(rb_cHash, "[]", rb_hash_s_create, -1);
    rb_define_method(rb_cHash,"initialize", rb_hash_initialize, -1);
    rb_define_method(rb_cHash,"initialize_copy", rb_hash_replace, 1);
    rb_define_method(rb_cHash,"rehash", rb_hash_rehash, 0);

    rb_define_method(rb_cHash,"to_hash", rb_hash_to_hash, 0);
    rb_define_method(rb_cHash,"to_a", rb_hash_to_a, 0);
    rb_define_method(rb_cHash,"to_s", rb_hash_to_s, 0);
    rb_define_method(rb_cHash,"inspect", rb_hash_inspect, 0);

    rb_define_method(rb_cHash,"==", rb_hash_equal, 1);
    rb_define_method(rb_cHash,"[]", rb_hash_aref, 1);
    rb_define_method(rb_cHash,"fetch", rb_hash_fetch, -1);
    rb_define_method(rb_cHash,"[]=", rb_hash_aset, 2);
    rb_define_method(rb_cHash,"store", rb_hash_aset, 2);
    rb_define_method(rb_cHash,"default", rb_hash_default, -1);
    rb_define_method(rb_cHash,"default=", rb_hash_set_default, 1);
    rb_define_method(rb_cHash,"default_proc", rb_hash_default_proc, 0);
    rb_define_method(rb_cHash,"index", rb_hash_index, 1);
    rb_define_method(rb_cHash,"indexes", rb_hash_indexes, -1);
    rb_define_method(rb_cHash,"indices", rb_hash_indexes, -1);
    rb_define_method(rb_cHash,"size", rb_hash_size, 0);
    rb_define_method(rb_cHash,"length", rb_hash_size, 0);
    rb_define_method(rb_cHash,"empty?", rb_hash_empty_p, 0);

    rb_define_method(rb_cHash,"each", rb_hash_each_pair, 0);
    rb_define_method(rb_cHash,"each_value", rb_hash_each_value, 0);
    rb_define_method(rb_cHash,"each_key", rb_hash_each_key, 0);
    rb_define_method(rb_cHash,"each_pair", rb_hash_each_pair, 0);
    rb_define_method(rb_cHash,"sort", rb_hash_sort, 0);

    rb_define_method(rb_cHash,"keys", rb_hash_keys, 0);
    rb_define_method(rb_cHash,"values", rb_hash_values, 0);
    rb_define_method(rb_cHash,"values_at", rb_hash_values_at, -1);

    rb_define_method(rb_cHash,"shift", rb_hash_shift, 0);
    rb_define_method(rb_cHash,"delete", rb_hash_delete, 1);
    rb_define_method(rb_cHash,"delete_if", rb_hash_delete_if, 0);
    rb_define_method(rb_cHash,"select", rb_hash_select, -1);
    rb_define_method(rb_cHash,"reject", rb_hash_reject, 0);
    rb_define_method(rb_cHash,"reject!", rb_hash_reject_bang, 0);
    rb_define_method(rb_cHash,"clear", rb_hash_clear, 0);
    rb_define_method(rb_cHash,"invert", rb_hash_invert, 0);
    rb_define_method(rb_cHash,"update", rb_hash_update, 1);
    rb_define_method(rb_cHash,"replace", rb_hash_replace, 1);
    rb_define_method(rb_cHash,"merge!", rb_hash_update, 1);
    rb_define_method(rb_cHash,"merge", rb_hash_merge, 1);

    rb_define_method(rb_cHash,"include?", rb_hash_has_key, 1);
    rb_define_method(rb_cHash,"member?", rb_hash_has_key, 1);
    rb_define_method(rb_cHash,"has_key?", rb_hash_has_key, 1);
    rb_define_method(rb_cHash,"has_value?", rb_hash_has_value, 1);
    rb_define_method(rb_cHash,"key?", rb_hash_has_key, 1);
    rb_define_method(rb_cHash,"value?", rb_hash_has_value, 1);

#ifndef __MACOS__ /* environment variables nothing on MacOS. */
    origenviron = environ;
    envtbl = rb_obj_alloc(rb_cObject);
    rb_extend_object(envtbl, rb_mEnumerable);

    rb_define_singleton_method(envtbl,"[]", rb_f_getenv, 1);
    rb_define_singleton_method(envtbl,"fetch", env_fetch, -1);
    rb_define_singleton_method(envtbl,"[]=", env_aset, 2);
    rb_define_singleton_method(envtbl,"store", env_aset, 2);
    rb_define_singleton_method(envtbl,"each", env_each, 0);
    rb_define_singleton_method(envtbl,"each_pair", env_each, 0);
    rb_define_singleton_method(envtbl,"each_key", env_each_key, 0);
    rb_define_singleton_method(envtbl,"each_value", env_each_value, 0);
    rb_define_singleton_method(envtbl,"delete", env_delete_m, 1);
    rb_define_singleton_method(envtbl,"delete_if", env_delete_if, 0);
    rb_define_singleton_method(envtbl,"clear", env_clear, 0);
    rb_define_singleton_method(envtbl,"reject", env_reject, 0);
    rb_define_singleton_method(envtbl,"reject!", env_reject_bang, 0);
    rb_define_singleton_method(envtbl,"select", env_select, -1);
    rb_define_singleton_method(envtbl,"shift", env_shift, 0);
    rb_define_singleton_method(envtbl,"invert", env_invert, 0);
    rb_define_singleton_method(envtbl,"replace", env_replace, 1);
    rb_define_singleton_method(envtbl,"update", env_update, 1);
    rb_define_singleton_method(envtbl,"inspect", env_inspect, 0);
    rb_define_singleton_method(envtbl,"rehash", env_none, 0);
    rb_define_singleton_method(envtbl,"to_a", env_to_a, 0);
    rb_define_singleton_method(envtbl,"to_s", env_to_s, 0);
    rb_define_singleton_method(envtbl,"index", env_index, 1);
    rb_define_singleton_method(envtbl,"indexes", env_indexes, -1);
    rb_define_singleton_method(envtbl,"indices", env_indexes, -1);
    rb_define_singleton_method(envtbl,"size", env_size, 0);
    rb_define_singleton_method(envtbl,"length", env_size, 0);
    rb_define_singleton_method(envtbl,"empty?", env_empty_p, 0);
    rb_define_singleton_method(envtbl,"keys", env_keys, 0);
    rb_define_singleton_method(envtbl,"values", env_values, 0);
    rb_define_singleton_method(envtbl,"values_at", env_values_at, -1);
    rb_define_singleton_method(envtbl,"include?", env_has_key, 1);
    rb_define_singleton_method(envtbl,"member?", env_has_key, 1);
    rb_define_singleton_method(envtbl,"has_key?", env_has_key, 1);
    rb_define_singleton_method(envtbl,"has_value?", env_has_value, 1);
    rb_define_singleton_method(envtbl,"key?", env_has_key, 1);
    rb_define_singleton_method(envtbl,"value?", env_has_value, 1);
    rb_define_singleton_method(envtbl,"to_hash", env_to_hash, 0);

    rb_define_global_const("ENV", envtbl);
#else /* __MACOS__ */
	envtbl = rb_hash_s_new(0, NULL, rb_cHash);
    rb_define_global_const("ENV", envtbl);
#endif  /* ifndef __MACOS__  environment variables nothing on MacOS. */
}
