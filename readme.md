## A Compact C Utilities Library

Since we are now in the 21st century, llib uses C99, but is compatible with
MSVC when compiled as C++.  This leads to some 'unnecessary' casting to keep
C++ happy, but working in the intersection between C99 and MSVC C++ makes this
library very portable.  The features it depends on are the freedom to declare variables in
for-loops and variadic preprocessor macros.

It has been tested on Windows 7 64-bit (mingw and MSVC), and both Linux 32-bit and
64-bit.

There are a number of kitchen-sink extended C libraries like glibc and the
Apache Portable Runtime but these are big and awkward to use on all platforms.
llib aims to be small as possible and intended for _static_ linking with your
application (the BSD licensing should help with this).

llib provides a refcounting mechanism, extended string functions, dynamically-resizable arrays,
doubly-linked lists and maps using binary trees. The aim of this first release is
not to produce the most efficient code possible but to explore the API and validate
the refcounting semantics.

Using this basic mechanism will cost your program less than 10Kb; a Linux executable
using features from the whole library is less than 40Kb.

A note on style: although C has been heavily influenced by C++ over the years,
it remains C. So judicious use of statement macros like `FOR` is fine, if they
are suitably hygenic:

```C
#define FOR(i,n) for (int i = 0, n_ = (n); i < n_; i++)
```

## Reference Counting

llib provides refcounted arrays, which contain their own size. You do not ever use
`free` on objects created by llib, and instead use `obj_unref`.

```C
#include <stdio.h>
#include <llib/obj.h>

int main()
{
    int *ai = array_new(int,10);
    for (int i = 0, n = array_len(ai); i < n; ++i)
        ai[i] = 10.0*i;
    ...
    obj_unref(ai);
    return 0;
}
```
C does not have smart pointers, but dumb pointers with hidden secrets. All objects
are over-allocated, with a hidden header behind the pointer. This keeps track of its
size if it is an array, and there is a type descriptor indexing a type object
which contains a 'destructor' for the object. Objects start out with a reference
count of 1, and the macro `obj_ref` increments this count; if un-referenced and the
count is zero, then we call the destructor and free the memory.


```C
typedef struct ObjHeader_ {
    unsigned int type:14;
    unsigned int is_array:1;
    unsigned int is_ref_container:1;
    unsigned int _ref:16;
    unsigned int _len:32;
} ObjHeader;
```

This allows for objects to safely _share_ other objects without having to fully
take ownership.  Reference counting cuts down on the amount of 'defensive copying'
needed in typical code.

Unless you specifically define `OBJ_REF_ABBREV`, the basic operations are named
`ref` and `unref`; there is also an alias `dispose` for `obj_unref_v` which
un-references multiple objects.

It is straightforward to create new objects which fit with llib's object scheme:

```C
typedef struct {
    int *ages;
} Bonzo;

static void Bonzo_dispose(Bonzo *self) {
    printf("disposing Bonzo\n");
    unref(self->ages);
}

Bonzo *Bonzo_new(int *ages) {
    Bonzo *self = obj_new(Bonzo,Bonzo_dispose);
    self->ages = ref(ages);
    return self;
}

void test_bonzo() {
    int AGES[] = {45,42,30,16,17};
    int *ages = array_new(int,5);

    FOR(i,5) ages[i] = AGES[i];  //??

    Bonzo *b = Bonzo_new(ages);

    unref(ages);

    printf("%d %d\n",b->ages[0],b->ages[1]);

    unref(b);
}
```

There is nothing special about such structures; when creating them with the macro
`obj_new` you can provide a dispose function. In this case, `Bonzo` objects
reference the array of ages, and they un-reference this array when they are finally
disposed.  The test function releases the array `ages`, and thereafter the only
reference to `ages` is kept within the struct.

Arrays can be _reference containers_ which hold refcounted objects:

```C
    Bonzo *dogs = array_new_ref(Bonzo,3);
    // see? They all happily share the same ages array!
    dogs[0] = Bonzo_new(ages);
    dogs[1] = Bonzo_new(ages);
    dogs[2] = Bonzo_new(ages);
    ...
    // this un-references the Bonzo objects
    // only then the array will die
    unref(dogs);
```

## Sequences

llib provides resizable arrays, called 'seqs'.

```C
float **s = seq_new(float);
seq_add(s,1.2);
seq_add(s,5.2);
....
float *arr = ref(*s);
unref(s);

// array still lives!
FOR(i,array_len(arr)) printf("%f ",arr[i]);
printf("\n");

// alternative macro
// FOR_ARR(float,pf,arr) printf("%f ",*pf);

```

You treat seqs like a pointer to an array, and use `seq_add` to ensure that the
array is resized when needed.  `*s` is _always_ a valid llib array, and in particular
`array_len` returns the correct size.

A seq keeps a reference to this array, and to get a reference to the array you
can just say `ref(*s)` and then it's fine to dispose of the seq itself. The function
`seq_array_ref` combines these two operations of sharing the array and disposing
the seq, plus resizing to fit.

These can also explicitly be _reference containers_ which derereference their objects
afterwards if you create them using `seq_new_ref`.

## Linked Lists

A doubly-linked list is a very useful data structure which offers fast insertion at
arbitrary posiitions.  It is sufficiently simple that it is continuously
reinvented, which is one of the endemic C diseases.

```C
    List *l = list_new_str();
    list_add (l,"two");
    list_add (l,"three");
    list_insert_front(l,"one");
    printf("size %d 2nd is '%s'\n",list_size(l),list_get(l,1));
    FOR_LIST(pli, l)
        printf("'%s' ",pli->data);
    printf("\n");
    unref(l);
    printf("remaining %d\n",obj_kount());
    return 0;
//    size 3 2nd is 'two'
//    'one' 'two' 'three'
//    remaining 0
```

A list of strings is a ref container, but with the added thing that if we try to add
a string which is not _one of ours_ then a proper refcounted copy is made. So it is
safe to add strings from any source, such as a local buffer on the heap. These are all
properly disposed of with the list.

Generally, containers in C are annoying, because of the need for typecasts. (Already
with `-Wall` GCC is giving us some warnings about those `printf` flags.)  Integer
types can fit into the pointer payload fine enough, but it isn't possible to
directly insert floating-point numbers.  List wrappers do a certain amount of pointer
aliasing magic for us:

```C
    float ** pw = (float**)listw_new();
    listw_add(pw, 10);
    listw_add(pw, 20);
    listw_add(pw, 30);
    listw_add(pw, 40);
    listw_insert(pw, listw_first(pw), 5);
    printf("first %f\n",listw_get(pw,0));
    FOR_LISTW(p, pw)
        printf("%f\n",**pw);

```

They are declared as if they were seqs (pointers to arrays) and there's a way
to iterate over typed values.

## Maps

Maps may have two kinds of keys; integer/pointer, and strings. Like string lists,
the latter own their key strings and they will be safely disposed of later. They
may contain integer/pointer values, or string values. The difference again is
with the special semantics needed to own arbitrary strings.

Typecasting is again irritating, so there are macros `map_puti` etc for the
common integer-valued case:

```C
    Map *m = map_new_str_ptr();
    map_puti(m,"hello",23);
    map_puti(m,"alice",10);
    map_puti(m,"frodo",2353);

    printf("lookup %d\n",map_geti(m,"alice"));

    map_remove(m,"alice");
    FOR_MAP(mi,m)
        printf("key %s value %d\n",mi->key,mi->value);
    unref(m);
```

The implementation in llib is a binary tree - not in general the best, but it works
reliably and has defined iteration order.

Maps can be initialized from arrays of `MapkeyValue` structs. Afterwards, such an
array can be generated using `map_to_array`:

```C
    MapKeyValue mk[] = {
        {"alpha","A"},
        {"beta","B"},
        {"gamma","C"},
        {NULL,NULL}
    };
    Map *m = map_new_str_str();
    map_put_keyvalues(m,mk);

    MapKeyValue *pkv = map_to_array(m);

    for (MapKeyValue *p = pkv; p->key; ++p)
        printf("%s='%s' ",p->key,p->value);
    printf("\n");

    unref(m);
    unref(pkv);
```

llib also provides 'simple maps' which are arrays of strings where the even elements are the keys and the odd elements are the values;  `str_lookup` will look up these values by linear search, which is efficient enough for small arrays.

## Strings

Strings are the usual nul-terminated char arrays, but llib refcounted strings are arrays and so
already know their size through `array_len` (which will be faster than `strlen` for long strings.)
It's generally hard to know portably if an arbitrary pointer is heap-allocated, so there's
`str_ref` which does the 'one of mine?' check and makes a refcounted copy if not, otherwise acts
just like `obj_ref`.

`str_fmt` is a convenient and safe way to do `sprintf`, and will return a refcounted string.

String support in C is famously minimalistic, so llib adds some extensions. `str_split` uses a
delimiter to split a string into an array using delimiter chars. The array is a ref container so the
strings will be disposed with it:

```C
    Str* words = str_split("alpha, beta, gamma",", ");
    assert(array_len(words) == 3);
    assert(str_eq(words[0],"alpha"));
    assert(str_eq(words[1],"beta"));
    assert(str_eq(words[2],"gamma"));
    unref(words);
```

Building up strings is a common need, and llib provides two ways to do it.  If you already have
an array of strings then feed it to `str_concat` with a delimiter - it is the inverse operation
to `str_split`. If the string is built up in an ad-hoc fashion then use the `strbuf_*` functions.
A `string buffer` is basically a sequence, so that `strbuf_add` is exactly the same as `seq_add`
for character arrays.  `strbuf_adds` appends a string, and `strbuf_addf` is equivalent to
`strbuf_adds(ss,str_fmt(fmt,...))`.

They are used for operations which modify strings, like inserting, removing and replacing, and
resemble the similar methods of C++'s `std::string`.

Then there are operations on strings which don't modify them:

```C
    const char *S = "hello dolly";
    int p = str_findstr(S,"doll");
    assert(p == 6);
    p = str_findch(S,'d');
    assert(p == 6);
    p = str_find_first_of("hello dolly"," ");
    assert(p == 5);
    assert(str_starts_with(S,"hell"));
    assert(str_ends_with(S,"dolly"));
```

They're simple wrappers over the old `strchr`, `strstr` etc functions that return offsets
rather than pointers. This is a more appropriate style for refcounted strings where you
want to only use the allocated 'root' pointer.

## String Templates

These are strings where occurences of a `$(var)` pattern are expanded.  Sometimes called
'string interpolation', it's a good way to generate large documents like HTML output and does not
suffer from the format string size limitations of the `printf` family.
You make a template object from a template string, and expand a template using that
object plus a function to map string keys to values, and a 'map' object.
The default form assumes that the object is just a NULL-terminated array of strings listing the keys
and values, and plain linear lookup.

```C
    const char *tpl = "Hello $(name), how is $(home)?";
    char *tbl1[] = {"name","Dolly","home","here",NULL};
    StrTempl st = str_templ_new(tpl,NULL);
    char *S = str_templ_subst(st,tbl1);
    assert(str_eq(S,"Hello Dolly, how is here?"));
```
You don't have to use `$()` as the magic characters, which is the default indicated by `NULL` in
`str_templ_new`. For instance, if expanding templates containing JavaScript it's better to use `@()`
which will not conflict with use of JQuery.

We can easily use a llib map with the more general form:

```C
    Map *m = map_new_str_str();
    map_put(m,"name","Monique");
    map_put(m,"home","Paris");
    S = str_templ_subst_using(st, (StrLookup)map_get, m);
    assert(str_eq(S,"Hello Monique, how is Paris?"));
```

String interpolation is more common in dynamic languages, but perfectly possible to do in less
flexible static languages like C, as long as there is some mechanism available for string lookup.
You can even use `getenv` to expand environment variables, but it does not quite have the
right signature.  Generally if any lookup fails, the replacement is the empty string.

```C
static char *l_getenv (void *data, char *key) {
    return getenv(key);
}

void using_environment()
{
    StrTempl st = str_templ_new("hello @{USER}, here is @{HOME}","@{}");
    char *S = str_templ_subst_using(st, (StrLookup)l_getenv, NULL);
    printf("got '%s'\n",S);
    dispose(st,S);
}
```
An interesting experimental feature is the ability to define _subtemplates_. Say we have the
following template:

```html
<h2>$(title)</h2>
<ul>
$(for items |
<li><a src="$(url)">$(title)</a></li>
|)
</ul>
```

Then `items` must be something _iteratable_ returning something _indexable_. The text
inside "|...|" is the subtemplate and any variable expansion inside it will look up names
in the objects returned by the iteration.

```C
char ***smap = array_new_ref(char**,2);
smap[0] = str_strings("title","Index","url","catalog.html",NULL);
smap[1] = str_strings("title","home","url","index.html",NULL);

char *tbl = {
    "title", "Pages",
    "items",(char*)smap,
    NULL
};
```

Currently _iteratable` means either an array or list of objects; _indexable_ means either
a 'simple map' or a llib `Map`.  This kind of dynamic behaviour is not C's greatest
strength, due to the lack of runtime information, but llib objects do have hidden RTTI.

Here we exploit several known facts:

  * `obj_refcount() will return a count greater than zero if it is one of our objects
  *  refcounted arrays know their size; otherwise we assume NULL-terminated
  *  `list_object` and `map_object` can identify their respective types

The rest is poor man's OOP - see the last page of `template.c`.  In C this is often considered
to be C++ envy, but OOP  predates C++ and is a good general strategy in this case;
it just happens to be a bit more clumsy without built-in support.

## Values

llib values have dynamic types since they have their type encoded in their headers.
A value can be an array of ints, a string, or a map of other values, and so forth, and
its type and structure can be inspected at run-time.
To represent numbers we have to introduce wrappers for integer, float and boolean 
types, rather as it is done in Java.  This being C, the programmer has to explicitly box floats
(for example) using `value_float` and so forth, and unbox with `value_as_float`. 

Ref-counted strings are llib values, although string literals need an explicit `str_new`.

The typedef `PValue` is just `void*`. There is a special type 'error', so using values is a
flexible way for a C function to return a sensible error message.

```C
PValue v = my_function();
if (value_is_error(v)) {
    fprintf(stderr,"my_function() is borked: %s\n",(char*)v);
} else { // we're cool
    double res = value_as_float(v);
    ...
}
```

Note that an error value is just a ref-counted string, with a distinct _type_.  llib types are distinct
if they have different _names_, so the trick is to define a standard type slot `OBJ_ECHAR_T` 
where the type name is "echar_ *" and then make an error like so:

```C
PValue value_error (const char *msg) {
    PValue v = str_new(msg);
    obj_set_type(v,OBJ_ECHAR_T);
    return v;
}
```

(`my_function` could actually return floats, arrays, ad absurdum and the caller could 
distinguish between these using the `value_is_xxx` functions.  But this is not a good idea
in general and causes confusion even with dynamic languages.)

With values, you can have the same kind of dynamic ad-hoc data structures that are common
in dynamic languages.  For instance, this is a neater way to specify the data for the
HTML template just discussed:

```C
    PValue v = VM(
        "title",VS("Pages"),
        "items",VA(
            VMS("url","index.html","title","Home"),
            VMS("url","catalog.html","title","Links")
        )
    );
```

This uses llib's JSON support, which works very naturally with values. And JSON itself is a
great notation to express dynamic data structures (although many people find the
_ad-hoc_ part less of a solution and more of a problem):

```C
    const char *js = "{'title':'Pages','items':[{'url':'index.html','title':'Home'},"
        "{'url':'catalog.html','title':'Links'}]}";

    ....
    v = json_parse_string(js);
    S = str_templ_subst_values(st,v);
```

Another useful property of values when used in templates is that they know how 
to turn themselves  into a string.  With plain data, we have to assume that the
expansions result in a string,
but if they do result in a value, then `value_tostring` will be used. (The template function
`$(i var)` will explicitly convert integers to strings, but it's hard to work with
floating-point numbers this way).

## XML
 
This is a large and opinionated subject, so let me state that what most people need is 'pointy-bracket data language' (PBDL) rather than full-blown schemas-and-transforms (XML). In particular, it is a common configuration format.  When used in that way, we can simplify life by ignoring empty text elements and comments - since it is the structure of the _data_ that is important.  If test.xml is:

```xml
<root>
    <item name='age' type='int'>10</item>
    <item name='name' type='string'>Bonzo</item>
</root>
```

then `xml_parse_file("test.xml",true)` will return a `root` element with two `item` child elements. The representation is a little unusual but straightforward; an element is an array of objects, with the first item being the tag name, the second (optional) item being an array of atributes (so-called 'simple map') and the remainder contains the child nodes - these are either strings or elements themselves.  So `test.xml` is completely equivalent to this data constructor:

```C
    v = VAS("root",
        VAS("item",VMS("name","age","type","int"),"10"),
        VAS("item",VMS("name","name","type","string"),"Bonzo")
    );
```

and the output with `json_tostring` is

```C
["root",["item",{"name":"age","type":"int"},"10"],["item",{"name":"name","type":"string"},"Bonzo"]]
```

## Command-line Parsing

There are standard ways of processing command-line arguments in POSIX, but they're fairly primitive and not available directly on Windows. The `arg` module gives a higher-level way of specifying arguments, which allows you to bind C variables to named flags and arguments. Here is a modified head-like utility:

```C
// cmd.c
#include <stdio.h>
#include <llib/arg.h>

int lines;
FILE *file;
bool print_lines;

ArgFlags args[] = {
    {"int lines=10",'n',&lines,"number of lines to print"},
    {"bool lineno",'l',&print_lines,"output line numbers"},
    {"infile #1=stdin",0,&file,"file to dump"},
    {NULL,0,NULL,NULL}
};

int main(int argc,  const char **argv)
{
    args_command_line(args,argv);
    char buff[512];
    int i = 1;
    while (fgets(buff,sizeof(buff),file)) {
        if (print_lines)
            printf("%03d\t%s",i,buff);
        else
            printf("%s",buff);
        if (i++ == lines)
            break;  
    }
    fclose(file);
    return 0;
}
```

This was inspired by the [lapp](?) framework for Lua; one specifies the arguments in a way that they can be used both for printing out help automatically and with enough type information that values can be parsed correctly.  So `cmd -l x` is an error because 'x' is not a valid integer; `cmd temp.txt` is an error if `temp.txt` cannot be opened for reading.  So we keep the logic of the program as straightforward as possible; note how the type and default value is specified using pseudo-C notation (the default value for a boolean flag is `false` but can be set to `true`.)

Otherwise, the flag parsing is GNU style, with long flags using '--' and short aliases with '-'. Short flags can be combined '-abc' and their values can follow immediately after '-I/usr/include/lua'.

A flag specifier like `string include[]` binds to an array of strings (`char**`) and a default cannot be specified; if the flag is not present the variable is initialized to a array of zero length. llib arrays know their size, so we don't have to track this separately.

Flags can also be implemented by _functions_, which you can see in action in `examples/testa.c`. Unlike variable flags, these can take an indefinite number of arguments. _Commands_ are related, where a program exposes its functionality with subcommands. for instance 'git status'.  `testa` shows how a simple but effective interactive prompt can be produced by spliting the line and parsing it explicitly.

Since llib is linked statically by default, the resulting programs remain small; `cmd` is only 22Kb on a 32-bit Linux system.



