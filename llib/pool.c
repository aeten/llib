/*
* llib little C library
* BSD licence
* Copyright Steve Donovan, 2013
*/
#include <stdlib.h>
#include "obj.h"

////// Object Pool Support //////
// An object pool is a ref seq containing all objects generated since the pool
// was created.  The actual pool object is a ref to that seq, so that disposing
// of it will drain the pool.
//
// As objects are explicitly unref'd, they're taken out of the pool by setting
// their entry to NULL.  So when we finally drain the pool, it only contains
// genuine alive orphan objects.
//
//

extern DisposeFn _pool_filter, _pool_cleaner;

typedef void* ObjPool;

typedef void*** VoidSeq;

static VoidSeq _obj_pool;
static VoidSeq _pool_stack;
static ObjPool *_pool_marker;

static void seq_stack_push(VoidSeq stack, void *value) {
    seq_add(_pool_stack,_obj_pool);
}

static void *seq_stack_pop(VoidSeq stack) {
    void **arr = *stack;
    int top = array_len(arr)-1;
    if (top == -1) // empty?
        return NULL;
    void *val = arr[top];
    array_len(arr) = top;
    return val;
}

static void pool_add(void *P) {
    seq_add(_obj_pool, P);
}

static void pool_clean(void *P) {
    void **objs = *_obj_pool;
    FOR(i,array_len(objs)) {
        if (objs[i] == P) {
            objs[i] = NULL;
            break;
        }
    }
}

static void pool_dispose(ObjPool * p) {
    // NB not to try any cleanup at this point!
    _pool_cleaner = NULL;
    obj_unref(*p);  // kill the actual pool (ref seq containing objects)
    _pool_cleaner = pool_clean;

    _obj_pool = (VoidSeq)seq_stack_pop(_pool_stack);

    if (_obj_pool == NULL) { // stop using the pool; it's dead!
        _pool_marker = NULL;
        _pool_filter = NULL;
        _pool_cleaner = NULL;
        obj_unref(_pool_stack);
        _pool_stack = NULL;
    }
}

/// create an object pool which will collect all references generated by llib
void *obj_pool() {
    if (! _pool_stack) {
        _pool_stack = seq_new(void*);
    }
    // push  the current pool on the stack (will always be NULL initially)
    seq_stack_push(_pool_stack,_obj_pool);
    _obj_pool = seq_new_ref(void*);
    // the new pool is referenced by this object which controls
    // the pool's lifetime
    _pool_marker = obj_new(ObjPool,pool_dispose);
    *_pool_marker = _obj_pool;
    // the core will access the pool through these function pointers
    _pool_filter = pool_add;
    _pool_cleaner = pool_clean;
    return (void*)_pool_marker;
}

// this is a helper for the magic 'scoped' macro
void __auto_unref(void *p)  {
    return obj_unref(*(void**)p);
}
