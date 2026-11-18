#include "vm.h"
#include "profiler.h"
#include "value.h"
#include "lexer.h"
#include "module.h"
#include <dlfcn.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
typedef struct {
    GSCode * code;
    size_t ip, base;
    GSEnvironment * env;
}
Frame;
typedef struct {
    char * name;
    void * handle;
    GSValue value;
}
NativeModule;
/* Runtime state: operand stack, call frames, globals, loaded modules, and diagnostics. */
struct GSVM {
    GSValue * stack;
    size_t sp, stack_cap;
    Frame * frames;
    size_t fp, frame_cap;
    GSEnvironment * globals;
    NativeModule * modules;
    size_t module_count, module_capacity;
    int errors, trace;
};
/* Performs the operation described by this helper and manages its VM values. */
static int vm_call(void * context, GSValue function, GSValue * args, size_t argc, GSValue * out);
/* Performs the operation described by this helper and manages its VM values. */
static void push(GSVM * v, GSValue x) {
    if(v->sp == v->stack_cap) {
        v->stack_cap *= 2;
        v->stack = realloc(v->stack, v->stack_cap * sizeof * v->stack);
        if( ! v->stack) exit(2);
    }
    v->stack[v->sp++] = x;
}
/* Performs the operation described by this helper and manages its VM values. */
static GSValue popv(GSVM * v) {
    return v->sp ? v->stack[-- v->sp] : gs_null();
}
/* Performs the operation described by this helper and manages its VM values. */
static void drop(GSValue x) {
    gs_release(x);
}
#define C_RED "\033[31m"
#define C_YELLOW "\033[33m"
#define C_CYAN "\033[36m"
#define C_MAGENTA "\033[35m"
#define C_DIM "\033[2m"
#define C_RESET "\033[0m"
/* Performs the operation described by this helper and manages its VM values. */
static const char * value_kind(GSValueType t) {
    switch(t) {
        case V_NULL : return "null";
        case V_NUMBER : return "number";
        case V_BOOL : return "bool";
        case V_STRING : return "string";
        case V_ARRAY : return "array";
        case V_OBJECT : return "object";
        case V_FUNCTION : return "function";
        case V_NATIVE : return "native function";
        case V_ITER : return "iterator";
        default : return "unknown";
    }
}
/* Performs the operation described by this helper and manages its VM values. */
static int frame_line(const Frame * f) {
    if( ! f || ! f->code || ! f->code->count) return 0;
    size_t at = f->ip ? f->ip - 1 : 0;
    if(at >= f->code->count) at = f->code->count - 1;
    return f->code->code[at] .line;
}
/* Performs the operation described by this helper and manages its VM values. */
static void print_stack(const GSVM * v) {
    if( ! v || ! v->fp) return;
    fprintf(stderr, C_MAGENTA "stack trace" C_RESET ":\n");
    for(size_t i = v->fp;
    i > 0;
    i--) {
        const Frame * f = & v->frames[i - 1];
        fprintf(stderr, "  " C_DIM "at" C_RESET " %s:%d\n", f->code->name, frame_line(f));
    }
}
/* Performs the operation described by this helper and manages its VM values. */
static void runtime(GSVM * v, Frame * f, const char * m) {
    int line = frame_line(f);
    fprintf(stderr, C_RED "Something went wrong while running your program" C_RESET " " C_YELLOW "[RUN001]" C_RESET " at %s:%d\n", f && f->code ? f->code->name : "<unknown>", line);
    fprintf(stderr, "  " C_YELLOW "%s" C_RESET "\n", m);
    print_stack(v);
    if(v) v->errors++;
}
/* Performs the operation described by this helper and manages its VM values. */
static GSValue native_print(GSValue * a, size_t n) {
    for(size_t i = 0;
    i < n;
    i++) {
        if(i) putchar(' ');
        gs_print(stdout, a[i]);
    }
    putchar('\n');
    return gs_null();
}
/* Performs the operation described by this helper and manages its VM values. */
static GSValue native_interact(GSValue * a, size_t n) {
    if(n > 0) {
        fputs(gs_cstring(a[0]), stdout);
        fflush(stdout);
    }
    char * line = NULL;
    size_t capacity = 0;
    ssize_t length = getline(& line, & capacity, stdin);
    if(length < 0) {
        free(line);
        return gs_null();
    }
    while(length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r')) line[-- length] = '\0';
    GSValue result = gs_string(line);
    free(line);
    return result;
}
/* Performs the operation described by this helper and manages its VM values. */
static GSValue native_len(GSValue * a, size_t n) {
    if( ! n) return gs_number(0);
    switch(a[0] .type) {
        case V_STRING : return gs_number((double) strlen(a[0] .as.string.data));
        case V_ARRAY : return gs_number((double) a[0] .as.array->count);
        case V_OBJECT : return gs_number((double) a[0] .as.object->count);
        default : return gs_number(0);
    }
}
/* Performs the operation described by this helper and manages its VM values. */
static GSValue native_str(GSValue * a, size_t n) {
    return n ? gs_string(gs_cstring(a[0])) : gs_string("");
}
/* Performs the operation described by this helper and manages its VM values. */
static GSValue native_num(GSValue * a, size_t n) {
    return n ? gs_number(gs_number_value(a[0])) : gs_number(0);
}
/* Performs the operation described by this helper and manages its VM values. */
static GSValue native_type(GSValue * a, size_t n) {
    if( ! n) return gs_string("null");
    const char * s = "null";
    switch(a[0] .type) {
        case V_NULL : s = "null";
        break;
        case V_NUMBER : s = "number";
        break;
        case V_BOOL : s = "bool";
        break;
        case V_STRING : s = "string";
        break;
        case V_ARRAY : s = "array";
        break;
        case V_OBJECT : s = "object";
        break;
        case V_FUNCTION : case V_NATIVE : s = "function";
        break;
        case V_ITER : s = "iterator";
        break;
    }
    return gs_string(s);
}
/* Core collection and iteration helpers. Native functions return owned GSValues. */
/* Performs the operation described by this helper and manages its VM values. */
static GSValue native_range(GSValue * a, size_t n) {
    double start = 0, stop = 0, step = 1;
    if(n == 1) {
        stop = gs_number_value(a[0]);
    }
    else if(n >= 2) {
        start = gs_number_value(a[0]);
        stop = gs_number_value(a[1]);
        if(n >= 3) step = gs_number_value(a[2]);
    }
    else return gs_array();
    GSValue out = gs_array();
    if(step == 0) {
        fprintf(stderr, "greenServeFE: range() step cannot be zero.\n");
        return out;
    }
    double x = start;
    size_t count = 0;
    while((step > 0 && x < stop) || (step < 0 && x > stop)) {
        if(count >= 100000) {
            fprintf(stderr, "greenServeFE: range() limit is 100000 items; returning the items generated so far.\n");
            break;
        }
        gs_array_push(& out, gs_number(x));
        count++;
        double next = x + step;
        if(next == x) break;
        x = next;
    }
    return out;
}
/* Performs the operation described by this helper and manages its VM values. */
static GSValue native_push(GSValue * a, size_t n) {
    if(n < 2 || a[0] .type != V_ARRAY) return gs_null();
    gs_array_push(& a[0], a[1]);
    return gs_number((double) gs_array_count(a[0]));
}
/* Performs the operation described by this helper and manages its VM values. */
static GSValue native_pop(GSValue * a, size_t n) {
    if( ! n || a[0] .type != V_ARRAY) return gs_null();
    return gs_array_pop(& a[0]);
}
/* Performs the operation described by this helper and manages its VM values. */
static GSValue native_keys(GSValue * a, size_t n) {
    GSValue out = gs_array();
    if( ! n || a[0] .type != V_OBJECT) return out;
    for(size_t i = 0;
    i < gs_object_count(a[0]);
    i++) {
        const char * k = gs_object_key(a[0], i);
        GSValue x = gs_string(k ? k : "");
        gs_array_push(& out, x);
        gs_release(x);
    }
    return out;
}
/* Performs the operation described by this helper and manages its VM values. */
static GSValue native_values(GSValue * a, size_t n) {
    GSValue out = gs_array();
    if( ! n || a[0] .type != V_OBJECT) return out;
    for(size_t i = 0;
    i < gs_object_count(a[0]);
    i++) {
        const char * k = gs_object_key(a[0], i);
        GSValue x = gs_object_get(a[0], k ? k : "");
        gs_array_push(& out, x);
        gs_release(x);
    }
    return out;
}
/* Performs the operation described by this helper and manages its VM values. */
static GSValue native_contains(GSValue * a, size_t n) {
    if(n < 2) return gs_bool(0);
    if(a[0] .type == V_STRING) return gs_bool(strstr(a[0] .as.string.data, gs_cstring(a[1])) != NULL);
    if(a[0] .type == V_ARRAY) {
        for(size_t i = 0;
        i < gs_array_count(a[0]);
        i++) {
            GSValue x = gs_array_get(a[0], i);
            int same = gs_equal(x, a[1]);
            gs_release(x);
            if(same) return gs_bool(1);
        }
        return gs_bool(0);
    }
    if(a[0] .type == V_OBJECT) {
        const char * k = gs_cstring(a[1]);
        for(size_t i = 0;
        i < gs_object_count(a[0]);
        i++) {
            const char * x = gs_object_key(a[0], i);
            if(x && strcmp(x, k) == 0) return gs_bool(1);
        }
        return gs_bool(0);
    }
    return gs_bool(0);
}
/* Performs the operation described by this helper and manages its VM values. */
static int has_suffix(const char * s, const char * suffix) {
    size_t a = strlen(s), b = strlen(suffix);
    return a >= b && ! strcmp(s + a - b, suffix);
}
/* Performs the operation described by this helper and manages its VM values. */
static int has_slash(const char * s) {
    return strchr(s, '/') != NULL;
}
/* Performs the operation described by this helper and manages its VM values. */
static void module_error(GSVM * v, Frame * f, const char * module, const char * detail) {
    fprintf(stderr, C_RED "I could not load the module" C_RESET " " C_YELLOW "[RUN001]" C_RESET " at %s:%d\n  " C_YELLOW "Module:" C_RESET " '%s'\n  " C_YELLOW "Reason:" C_RESET " %s\n", f->code->name, f->code->code[f->ip - 1] .line, module, detail);
    v->errors++;
}
/* Performs the operation described by this helper and manages its VM values. */
static int module_load(GSVM * v, Frame * f, const char * name, GSValue * out) {
    for(size_t i = 0;
    i < v->module_count;
    i++) if( ! strcmp(v->modules[i] .name, name)) { * out = gs_copy(v->modules[i] .value);
        return 1;
    }
    const char * envpath = getenv("GREENSERVE_MODULE_PATH");
    char candidates[8][4096];
    size_t count = 0;
    if(has_slash(name) || has_suffix(name, ".so")) {
        snprintf(candidates[count++], sizeof candidates[0], "%s", name);
    }
    else {
        snprintf(candidates[count++], sizeof candidates[0], "./%s.so", name);
        snprintf(candidates[count++], sizeof candidates[0], "./modules/%s.so", name);
        snprintf(candidates[count++], sizeof candidates[0], "%s.so", name);
        if(envpath && * envpath) {
            const char * p = envpath;
            while( * p && count < 8) {
                const char * q = strchr(p, ':');
                size_t n = q ?(size_t)(q - p) : strlen(p);
                if(n > 0 && n + 1 + strlen(name) + 3 < sizeof candidates[0]) {
                    memcpy(candidates[count], p, n);
                    candidates[count][n] = '/';
                    snprintf(candidates[count] + n + 1, sizeof candidates[0] - n - 1, "%s.so", name);
                    count++;
                }
                if( ! q) break;
                p = q + 1;
            }
        }
    }
    void * h = NULL;
    const char * last_error = "file not found";
    for(size_t i = 0;
    i < count;
    i++) {
        dlerror();
        h = dlopen(candidates[i], RTLD_NOW | RTLD_LOCAL);
        if(h) break;
        const char * e = dlerror();
        if(e) last_error = e;
    }
    if( ! h) {
        module_error(v, f, name, last_error);
        return 0;
    }
    dlerror();
    GSModuleInit init = (GSModuleInit) dlsym(h, GSFE_MODULE_INIT_SYMBOL);
    const char * e = dlerror();
    if(e || ! init) {
        module_error(v, f, name, e ? e : "missing module initializer");
        dlclose(h);
        return 0;
    }
    GSNativeAPI native_api = {
        GSFE_MODULE_ABI_VERSION, gs_null, gs_number, gs_bool, gs_string, gs_object, gs_array, gs_native, gs_copy, gs_retain, gs_release, gs_array_push, gs_array_count, gs_array_get, gs_array_set, gs_object_count, gs_object_key, gs_object_set, gs_object_get, gs_cstring, gs_truthy, gs_number_value, gs_print, v, vm_call
    };
    GSValue module = init(& native_api);
    if(module.type != V_OBJECT) {
        module_error(v, f, name, "initializer did not return an object");
        gs_release(module);
        dlclose(h);
        return 0;
    }
    if(v->module_count == v->module_capacity) {
        size_t c = v->module_capacity ? v->module_capacity * 2 : 8;
        NativeModule * m = realloc(v->modules, c * sizeof * v->modules);
        if( ! m) exit(2);
        v->modules = m;
        v->module_capacity = c;
    }
    v->modules[v->module_count] .name = strdup(name);
    if( ! v->modules[v->module_count] .name) exit(2);
    v->modules[v->module_count] .handle = h;
    v->modules[v->module_count] .value = gs_copy(module);
    v->module_count++; * out = module;
    return 1;
}
/* Recursive value cloning keeps nested class defaults isolated between instances. */
/* Performs the operation described by this helper and manages its VM values. */
static GSValue clone_value_depth(GSValue value, unsigned depth) {
    if(depth >= 64) return gs_copy(value);
    if(value.type == V_ARRAY) {
        GSValue out = gs_array();
        for(size_t i = 0;
        i < gs_array_count(value);
        i++) {
            GSValue item = gs_array_get(value, i), copy = clone_value_depth(item, depth + 1);
            gs_array_push(& out, copy);
            gs_release(item);
            gs_release(copy);
        }
        return out;
    }
    if(value.type == V_OBJECT) {
        GSValue out = gs_object();
        for(size_t i = 0;
        i < gs_object_count(value);
        i++) {
            const char * key = gs_object_key(value, i);
            GSValue item = gs_object_get(value, key), copy = clone_value_depth(item, depth + 1);
            gs_object_set(& out, key, copy);
            gs_release(item);
            gs_release(copy);
        }
        return out;
    }
    return gs_copy(value);
}
/* Performs the operation described by this helper and manages its VM values. */
static GSValue clone_value(GSValue value) {
    return clone_value_depth(value, 0);
}
/* Performs the operation described by this helper and manages its VM values. */
static int is_class_value(GSValue value) {
    GSValue flag = gs_object_get(value, "__gs_class");
    int yes = flag.type == V_BOOL && flag.as.boolean;
    gs_release(flag);
    return yes;
}
/* class(fields, methods, optional_parent) creates a lightweight class descriptor.
   Methods receive the instance explicitly as their first parameter. */
/* Performs the operation described by this helper and manages its VM values. */
static GSValue native_class(GSValue * a, size_t n) {
    GSValue fields = gs_object(), methods = gs_object(), parent = gs_null();
    if(n >= 3 && is_class_value(a[2])) {
        parent = gs_copy(a[2]);
        GSValue pf = gs_object_get(a[2], "fields"), pm = gs_object_get(a[2], "methods");
        if(pf.type == V_OBJECT) {
            gs_release(fields);
            fields = clone_value(pf);
        }
        if(pm.type == V_OBJECT) {
            gs_release(methods);
            methods = clone_value(pm);
        }
        gs_release(pf);
        gs_release(pm);
    }
    if(n >= 1 && a[0] .type == V_OBJECT) {
        for(size_t i = 0;
        i < gs_object_count(a[0]);
        i++) {
            const char * k = gs_object_key(a[0], i);
            GSValue v = gs_object_get(a[0], k);
            gs_object_set(& fields, k, v);
            gs_release(v);
        }
    }
    if(n >= 2 && a[1] .type == V_OBJECT) {
        for(size_t i = 0;
        i < gs_object_count(a[1]);
        i++) {
            const char * k = gs_object_key(a[1], i);
            GSValue v = gs_object_get(a[1], k);
            gs_object_set(& methods, k, v);
            gs_release(v);
        }
    }
    GSValue out = gs_object();
    gs_object_set(& out, "__gs_class", gs_bool(1));
    gs_object_set(& out, "fields", fields);
    gs_object_set(& out, "methods", methods);
    gs_object_set(& out, "parent", parent);
    gs_release(fields);
    gs_release(methods);
    gs_release(parent);
    return out;
}
/* Performs the operation described by this helper and manages its VM values. */
static GSValue native_new(GSValue * a, size_t n) {
    if( ! n || ! is_class_value(a[0])) return gs_null();
    GSValue fields = gs_object_get(a[0], "fields"), methods = gs_object_get(a[0], "methods");
    GSValue out = fields.type == V_OBJECT ? clone_value(fields) : gs_object();
    if(methods.type == V_OBJECT) for(size_t i = 0;
    i < gs_object_count(methods);
    i++) {
        const char * k = gs_object_key(methods, i);
        GSValue v = gs_object_get(methods, k);
        gs_object_set(& out, k, v);
        gs_release(v);
    }
    gs_object_set(& out, "__class", a[0]);
    gs_release(fields);
    gs_release(methods);
    return out;
}
/* Performs the operation described by this helper and manages its VM values. */
static GSValue native_instanceof(GSValue * a, size_t n) {
    if(n < 2 || a[0] .type != V_OBJECT || ! is_class_value(a[1])) return gs_bool(0);
    GSValue type = gs_object_get(a[0], "__class");
    int yes = 0;
    for(unsigned depth = 0;
    depth < 256 && is_class_value(type);
    depth++) {
        if(gs_equal(type, a[1])) {
            yes = 1;
            break;
        }
        GSValue next = gs_object_get(type, "parent");
        gs_release(type);
        type = next;
    }
    gs_release(type);
    return gs_bool(yes);
}
/* Performs the operation described by this helper and manages its VM values. */
static GSValue native_clone(GSValue * a, size_t n) {
    return n ? clone_value(a[0]) : gs_null();
}
/* Performs the operation described by this helper and manages its VM values. */
static GSValue native_has(GSValue * a, size_t n) {
    if(n < 2 || a[0] .type != V_OBJECT) return gs_bool(0);
    const char * k = gs_cstring(a[1]);
    for(size_t i = 0;
    i < gs_object_count(a[0]);
    i++) {
        const char * x = gs_object_key(a[0], i);
        if(x && ! strcmp(x, k)) return gs_bool(1);
    }
    return gs_bool(0);
}
/* Performs the operation described by this helper and manages its VM values. */
static GSValue native_merge(GSValue * a, size_t n) {
    if(n < 2 || a[0] .type != V_OBJECT || a[1] .type != V_OBJECT) return n ? gs_copy(a[0]) : gs_null();
    for(size_t i = 0;
    i < gs_object_count(a[1]);
    i++) {
        const char * k = gs_object_key(a[1], i);
        GSValue v = gs_object_get(a[1], k);
        gs_object_set(& a[0], k, v);
        gs_release(v);
    }
    return gs_copy(a[0]);
}
/* Performs the operation described by this helper and manages its VM values. */
static void install(GSEnvironment * g) {
    GSValue v = gs_native(native_class);
    env_define(g, "class", v);
    v = gs_native(native_new);
    env_define(g, "new", v);
    v = gs_native(native_instanceof);
    env_define(g, "instanceof", v);
    v = gs_native(native_clone);
    env_define(g, "clone", v);
    v = gs_native(native_has);
    env_define(g, "has", v);
    v = gs_native(native_merge);
    env_define(g, "merge", v);
    v = gs_native(native_print);
    env_define(g, "print", v);
    env_define(g, "show", v);
    v = gs_native(native_interact);
    env_define(g, "interact", v);
    v = gs_native(native_len);
    env_define(g, "len", v);
    v = gs_native(native_str);
    env_define(g, "str", v);
    v = gs_native(native_num);
    env_define(g, "num", v);
    v = gs_native(native_type);
    env_define(g, "type", v);
    v = gs_native(native_range);
    env_define(g, "range", v);
    v = gs_native(native_push);
    env_define(g, "push", v);
    v = gs_native(native_pop);
    env_define(g, "pop", v);
    v = gs_native(native_keys);
    env_define(g, "keys", v);
    v = gs_native(native_values);
    env_define(g, "values", v);
    v = gs_native(native_contains);
    env_define(g, "contains", v);
}
GSVM * vm_new(void) {
    GSVM * v = calloc(1, sizeof * v);
    if( ! v) exit(2);
    v->stack_cap = 256;
    v->stack = calloc(v->stack_cap, sizeof * v->stack);
    v->frame_cap = 64;
    v->frames = calloc(v->frame_cap, sizeof * v->frames);
    v->globals = env_new(NULL);
    install(v->globals);
    return v;
}
void vm_free(GSVM * v) {
    if( ! v) return;
    while(v->sp) drop(popv(v));
    for(size_t i = 0;
    i < v->fp;
    i++) env_release(v->frames[i] .env);
    env_release(v->globals);
    for(size_t i = 0;
    i < v->module_count;
    i++) {
        drop(v->modules[i] .value);
        free(v->modules[i] .name);
        dlclose(v->modules[i] .handle);
    }
    free(v->modules);
    free(v->stack);
    free(v->frames);
    free(v);
}
void vm_set_trace(GSVM * v, int x) {
    v->trace = x;
}
int vm_runtime_errors(const GSVM * v) {
    return v ? v->errors : 1;
}
/* Performs the operation described by this helper and manages its VM values. */
static void push_frame(GSVM * v, GSCode * c, GSEnvironment * e) {
    if(v->fp == v->frame_cap) {
        v->frame_cap *= 2;
        v->frames = realloc(v->frames, v->frame_cap * sizeof * v->frames);
        if( ! v->frames) exit(2);
    }
    v->frames[v->fp++] = (Frame) {
        c, 0, v->sp, e
    };
    env_retain(e);
    gsprof_enter_function(c ? c->name : NULL);
}
/* Performs the operation described by this helper and manages its VM values. */
static int do_call(GSVM * v, int argc, Frame * f) {
    if(v->sp < (size_t) argc + 1) {
        runtime(v, f, "stack underflow in call");
        return 0;
    }
    size_t at = v->sp - (size_t) argc - 1;
    GSValue fn = gs_copy(v->stack[at]);
    if(fn.type == V_NATIVE) {
        GSValue * rargs = & v->stack[at + 1];
        GSValue r = fn.as.native(rargs, (size_t) argc);
        gs_release(fn);
        for(size_t i = at;
        i < v->sp;
        i++) drop(v->stack[i]);
        v->sp = at;
        push(v, r);
        return 1;
    }
    if(fn.type != V_FUNCTION) {
        const char * kind = value_kind(fn.type);
        gs_release(fn);
        char msg[160];
        snprintf(msg, sizeof msg, "This value is a %s, so it cannot be called like a function. Check the value before using parentheses.", kind);
        runtime(v, f, msg);
        return 0;
    }
    GSCode * c = fn.as.function.code;
    GSEnvironment * e = env_new(fn.as.function.closure);
    for(int i = 0;
    i < c->param_count;
    i++) {
        GSValue a = i < argc ? v->stack[at + 1 + i] : gs_null();
        if(i < (int) c->name_count) env_define(e, c->names[i], a);
    }
    for(size_t i = at;
    i < v->sp;
    i++) drop(v->stack[i]);
    v->sp = at;
    push_frame(v, c, e);
    env_release(e);
    gs_release(fn);
    return 1;
}
/* Performs the operation described by this helper and manages its VM values. */
static int iter_next(GSVM * v, Frame * f, int jump) {
    if( ! v->sp) {
        runtime(v, f, "iterator stack underflow");
        return 0;
    }
    GSValue it = v->stack[v->sp - 1];
    if(it.type != V_ITER) {
        runtime(v, f, "value is not an iterator");
        return 0;
    }
    GSIterator * x = it.as.iter;
    GSValue item = gs_null();
    int ok = 0;
    if(x->iterable.type == V_ARRAY) {
        if(x->index < x->iterable.as.array->count) {
            item = gs_copy(x->iterable.as.array->items[x->index++]);
            ok = 1;
        }
    }
    else if(x->iterable.type == V_OBJECT) {
        if(x->index < x->iterable.as.object->count) {
            item = gs_string(x->iterable.as.object->items[x->index++] .key);
            ok = 1;
        }
    }
    else if(x->iterable.type == V_STRING) {
        size_t n = strlen(x->iterable.as.string.data);
        if(x->index < n) {
            char b[2] = {
                x->iterable.as.string.data[x->index++], 0
            };
            item = gs_string(b);
            ok = 1;
        }
    }
    if( ! ok) {
        f->ip = (size_t) jump;
        return 1;
    }
    push(v, item);
    return 1;
}
/* Performs the operation described by this helper and manages its VM values. */
static int vm_execute(GSVM * v, size_t stop_fp, GSValue * result) {
    while(v->fp > stop_fp) {
        Frame * f = & v->frames[v->fp - 1];
        if(f->ip >= f->code->count) {
            runtime(v, f, "instruction pointer out of range");
            return 1;
        }
        GSInstr in = f->code->code[f->ip++];
        gsprof_record_instruction(f->code, (GSOp) in.op, in.line, v->sp, v->fp);
        if(in.op == OP_CALL) gsprof_record_call();
        if(v->trace) printf(C_DIM "[%s %04zu] %-18s line=%d arg=%d" C_RESET "\n", f->code->name, f->ip - 1, op_name((GSOp) in.op), in.line, in.arg);
        switch((GSOp) in.op) {
            case OP_NOP : break;
            case OP_LOAD_CONST : push(v, gs_copy(f->code->constants[in.arg]));
            break;
            case OP_LOAD_NAME : {
                GSValue x;
                if( ! env_get(f->env, f->code->names[in.arg], & x)) {
                    char msg[256];
                    snprintf(msg, sizeof msg, "I could not find a variable named '%s'. Check the spelling, or make sure it is defined before this line.", f->code->names[in.arg]);
                    runtime(v, f, msg);
                    return 1;
                }
                push(v, x);
                break;
            }
            case OP_STORE_NAME : {
                if( ! v->sp) {
                    runtime(v, f, "stack underflow in store");
                    break;
                }
                if( ! env_set(f->env, f->code->names[in.arg], v->stack[v->sp - 1])) env_define(f->env, f->code->names[in.arg], v->stack[v->sp - 1]);
                break;
            }
            case OP_POP_TOP : drop(popv(v));
            break;
            case OP_ADD : case OP_SUB : case OP_MUL : case OP_DIV : case OP_MOD : case OP_EQ : case OP_NE : case OP_LT : case OP_LE : case OP_GT : case OP_GE : case OP_AND : case OP_OR : {
                GSValue b = popv(v), a = popv(v), r;
                if(in.op == OP_AND) r = gs_bool(gs_truthy(a) && gs_truthy(b));
                else if(in.op == OP_OR) r = gs_bool(gs_truthy(a) || gs_truthy(b));
                else {
                    int tok = 0;
                    switch((GSOp) in.op) {
                        case OP_ADD : tok = TOK_PLUS;
                        break;
                        case OP_SUB : tok = TOK_MINUS;
                        break;
                        case OP_MUL : tok = TOK_STAR;
                        break;
                        case OP_DIV : tok = TOK_SLASH;
                        break;
                        case OP_MOD : tok = TOK_PERCENT;
                        break;
                        case OP_EQ : tok = TOK_EQUAL_EQUAL;
                        break;
                        case OP_NE : tok = TOK_BANG_EQUAL;
                        break;
                        case OP_LT : tok = TOK_LESS;
                        break;
                        case OP_LE : tok = TOK_LESS_EQUAL;
                        break;
                        case OP_GT : tok = TOK_GREATER;
                        break;
                        case OP_GE : tok = TOK_GREATER_EQUAL;
                        break;
                        default : break;
                    }
                    r = gs_binary(tok, a, b);
                }
                drop(a);
                drop(b);
                push(v, r);
                break;
            }
            case OP_NEG : {
                GSValue a = popv(v), r = gs_number( - gs_number_value(a));
                drop(a);
                push(v, r);
                break;
            }
            case OP_NOT : {
                GSValue a = popv(v), r = gs_bool( ! gs_truthy(a));
                drop(a);
                push(v, r);
                break;
            }
            case OP_JUMP : f->ip = (size_t) in.arg;
            break;
            case OP_JUMP_IF_FALSE : {
                GSValue a = popv(v);
                int b = ! gs_truthy(a);
                drop(a);
                if(b) f->ip = (size_t) in.arg;
                break;
            }
            case OP_JUMP_IF_TRUE : {
                GSValue a = popv(v);
                int b = gs_truthy(a);
                drop(a);
                if(b) f->ip = (size_t) in.arg;
                break;
            }
            case OP_CALL : if( ! do_call(v, in.arg, f)) return 1;
            break;
            case OP_RETURN : {
                GSValue r = popv(v);
                Frame old = * f;
                gsprof_leave_function();
                env_release(old.env);
                v->fp--;
                v->sp = old.base;
                if(v->fp == stop_fp) {
                    if(result) * result = r;
                    else drop(r);
                    return v->errors ? 1 : 0;
                }
                push(v, r);
                break;
            }
            case OP_BUILD_ARRAY : {
                size_t n = (size_t) in.arg;
                if(v->sp < n) {
                    runtime(v, f, "stack underflow in array");
                    return 1;
                }
                size_t base = v->sp - n;
                GSValue a = gs_array();
                for(size_t i = base;
                i < v->sp;
                i++) gs_array_push(& a, v->stack[i]);
                for(size_t i = base;
                i < v->sp;
                i++) drop(v->stack[i]);
                v->sp = base;
                push(v, a);
                break;
            }
            case OP_BUILD_OBJECT : {
                size_t n = (size_t) in.arg;
                if(v->sp < 2 * n) {
                    runtime(v, f, "stack underflow in object");
                    return 1;
                }
                size_t base = v->sp - 2 * n;
                GSValue o = gs_object();
                for(size_t i = 0;
                i < n;
                i++) gs_object_set(& o, gs_cstring(v->stack[base + 2 * i]), v->stack[base + 2 * i + 1]);
                for(size_t i = base;
                i < v->sp;
                i++) drop(v->stack[i]);
                v->sp = base;
                push(v, o);
                break;
            }
            case OP_GET_INDEX : {
                GSValue idx = popv(v), obj = popv(v), r = gs_index(obj, idx);
                drop(obj);
                drop(idx);
                push(v, r);
                break;
            }
            case OP_SET_INDEX : {
                GSValue val = popv(v), idx = popv(v), obj = popv(v);
                gs_set_index(obj, idx, val);
                drop(val);
                drop(idx);
                push(v, obj);
                break;
            }
            case OP_GET_MEMBER : {
                GSValue obj = popv(v), r = gs_object_get(obj, f->code->names[in.arg]);
                drop(obj);
                push(v, r);
                break;
            }
            case OP_SET_MEMBER : {
                GSValue val = popv(v), key = popv(v), obj = popv(v);
                gs_object_set(& obj, gs_cstring(key), val);
                drop(val);
                drop(key);
                push(v, obj);
                break;
            }
            case OP_MAKE_FUNCTION : push(v, gs_function(f->code->children[in.arg], f->env));
            break;
            case OP_ITER_INIT : {
                GSValue x = popv(v);
                push(v, gs_iterator(x));
                drop(x);
                break;
            }
            case OP_ITER_NEXT : if( ! iter_next(v, f, in.arg)) return 1;
            break;
            case OP_IMPORT : {
                const char * n = gs_cstring(f->code->constants[in.arg]);
                GSValue mod;
                if( ! module_load(v, f, n, & mod)) return 1;
                env_define(f->env, n, mod);
                drop(mod);
                break;
            }
            case OP_FROM_IMPORT : {
                size_t n = (size_t) in.arg;
                if(v->sp < n + 1) {
                    runtime(v, f, "stack underflow in import");
                    return 1;
                }
                GSValue * names = calloc(n, sizeof * names);
                for(size_t i = n;
                i > 0;
                i--) names[i - 1] = popv(v);
                GSValue module_name = popv(v);
                GSValue module;
                if( ! module_load(v, f, gs_cstring(module_name), & module)) {
                    for(size_t i = 0;
                    i < n;
                    i++) drop(names[i]);
                    free(names);
                    drop(module_name);
                    return 1;
                }
                for(size_t i = 0;
                i < n;
                i++) {
                    GSValue name = names[i], val = gs_object_get(module, gs_cstring(name));
                    if(val.type == V_NULL) {
                        char msg[256];
                        snprintf(msg, sizeof msg, "The module does not export '%s'. Check the module name and the exported function or value.", gs_cstring(name));
                        runtime(v, f, msg);
                    }
                    else env_define(f->env, gs_cstring(name), val);
                    drop(val);
                    drop(name);
                }
                free(names);
                drop(module);
                drop(module_name);
                break;
            }
            case OP_HALT : return v->errors ? 1 : 0;
            default : runtime(v, f, "unknown opcode");
            return 1;
        }
    }
    return v->errors ? 1 : 0;
}
int vm_run(GSVM * v, GSCode * entry) {
    if( ! v || ! entry) return 1;
    push_frame(v, entry, v->globals);
    GSValue result = gs_null();
    int rc = vm_execute(v, 0, & result);
    for(size_t i = v->fp;
    i > 0;
    i--) gsprof_leave_function();
    if(result.type != V_NULL) gs_release(result);
    return rc;
}
/* Performs the operation described by this helper and manages its VM values. */
static int vm_call(void * context, GSValue function, GSValue * args, size_t argc, GSValue * out) {
    GSVM * v = (GSVM *) context;
    if( ! v || function.type != V_FUNCTION) return 0;
    size_t stop_fp = v->fp;
    GSEnvironment * env = env_new(function.as.function.closure);
    for(int i = 0;
    i < function.as.function.code->param_count;
    i++) {
        GSValue a = i < (int) argc ? args[i] : gs_null();
        if(i < (int) function.as.function.code->name_count) env_define(env, function.as.function.code->names[i], a);
    }
    push_frame(v, function.as.function.code, env);
    env_release(env);
    GSValue result = gs_null();
    int rc = vm_execute(v, stop_fp, & result);
    if(out) * out = result;
    else if(result.type != V_NULL) gs_release(result);
    return rc == 0;
}
