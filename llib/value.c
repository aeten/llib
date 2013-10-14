/*
* llib little C library
* BSD licence
* Copyright Steve Donovan, 2013
*/
#include "value.h"


static void Value_dispose(PValue v) {
    if (v->vc != ValueScalar || (v->type & ValueRef)) {
        obj_unref(v->v.ptr);
    }
}

PValue value_new(ValueType type, ValueContainer vc) {
    PValue v = obj_new(Value,Value_dispose);
    v->type = type;
    v->vc = vc;
    return v;
}

PValue value_str (const char *str) {
    PValue v = value_new(ValueString,ValueScalar);
    v->v.str = str_cpy((char*)str);
    return v;
}

PValue value_error (const char *msg) {
    PValue v = value_str(msg);
    v->type = ValueError;
    return v;
}

PValue value_float (double x) {
    PValue v = value_new(ValueFloat,ValueScalar);
    v->v.f = x;
    return v;
}

PValue value_int (long long i) {
    PValue v = value_new(ValueInt,ValueScalar);
    v->v.i = i;
    return v;
}

PValue value_bool (bool i) {
    PValue v = value_new(ValueBool,ValueScalar);
    v->v.i = i;
    return v;
}

PValue value_value (PValue V) {
    PValue v = value_new(ValueValue,ValueScalar);
    v->v.v = V;
    return v;
}

PValue value_list (List *ls, ValueType type) {
    PValue v = value_new(type,ValueList);
    v->v.ls = ls;
    return v;
}

PValue value_array (void *p, ValueType type) {
    PValue v = value_new(type,ValueArray);
    v->v.ptr = p;
    return v;
}

PValue value_map (Map *m, ValueType type) {
    PValue v = value_new(type,ValueMap);
    v->v.map = m;
    return v;
}
