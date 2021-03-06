/*
* llib little C library
* BSD licence
* Copyright Steve Donovan, 2013
*/

#include <stdio.h>
#include <assert.h>
#include <llib/obj.h>

typedef struct {
    int val;
} Foo;

void foo_dispose(Foo *p)
{
    printf("disposing foo %d\n",p->val);
}

static int kount = 0;

Foo *foo_new()
{
    Foo *obj = obj_new(Foo,foo_dispose);
    obj->val = ++kount;
    return obj;
}

#define DUMP(T,F,arr) FOR_ARR(T,p_,arr) \
    printf(F,*p_);  printf("\n");

typedef unsigned char byte;

int main()
{
// /*
    int **fs = seq_new(int);
    seq_add(fs, 10);
    seq_add(fs, 20);
    seq_add(fs, 30);
    seq_add(fs, 40);
    seq_add(fs, 50);
    FOR (i,22)
        seq_add(fs,i);
    // seq is always pointer to the array
    printf("size %d \n",array_len(*fs));
    DUMP(int,"%d ",*fs);
    obj_unref(fs);

    double **ss = seq_new(double);
    seq_add(ss,1.0);
    seq_add(ss,2.0);
    // can append arrays
    double *xx = array_new(double,2);
    xx[0] = 3.0;
    xx[1] = 4.0;
    seq_adda(ss,xx,-1); // -> -1 means use array_len of xx
    // and finally size-to-fit and extract the constructed array,
    // deferencing the seq.
    double *S = (double*)seq_array_ref(ss);
    DUMP(double,"%f ",S);

    // using seq as a stack...
    int **st = seq_new(int);
    seq_add(st,10);
    seq_add(st,20);
    seq_add(st,30);
    assert (seq_pop(st) == 30);
    assert (seq_pop(st) == 20);
    assert (seq_pop(st) == 10);

    byte **bb = seq_new(byte);
    byte b1[] = {10,20,30};
    byte b2[] = {40,50,60};
    seq_adda(bb,b1,3);
    seq_adda(bb,b2,3);
    DUMP(byte,"%d ",*bb);

    // ref seqs are containers for ref objects
    typedef Foo *PFoo;
    PFoo **sf = seq_new_ref(PFoo);
    FOR (i,6)
        seq_add(sf,foo_new());
    FOR_ARR(PFoo,p,*sf)
        printf("%d ",(*p)->val);
    printf("\n");
    PFoo f = ref((*sf)[2]);
    // and disposing them will dispose their objects
    unref(sf);
    printf("now explicitly release f\n");
    unref(f);

    printf("appending arrays to a container seq\n");
    sf = seq_new_ref(PFoo);
    FOR (i,2)
        seq_add(sf,foo_new());
    PFoo *extra = array_new_ref(PFoo,3);
    FOR (i,3)
        extra[i] = foo_new();
    seq_adda(sf,extra,-1);
    puts("unref extra");
    unref(extra);
    puts("seq has all references now");
    unref(sf);

    seq_remove(bb,1,1);
    assert((*bb)[1]==30);
    byte b = 21;
    seq_insert(bb,1,&b,1);
    assert((*bb)[1] == 21);

    return 0;
}
