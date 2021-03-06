#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/select.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>

#include <llib/list.h>

#include "select.h"

typedef unsigned char byte;

struct Pipe_ {
    int twrite;
    int tread;
};

struct SelectTimer_;

struct Select_ {
    fd_set fdset;
    int nfds;
    List *fds;  // (SelectFile) we are waiting on these for reading
    bool error;
    // timer support
    Pipe *tpipe;
    List *timers; //(SelectTimer)
    // once-off millisecond timer
    struct SelectTimer_ *milli_timer;
    struct timeval *p_timeval;
};

struct SelectTimer_ {
    SelectTimerProc callback;
    void *data;
    Pipe *tpipe;
    byte id;
    int secs;
    int pid;
    Select *s;
};

struct SelectFile_ {
    int fd;    
    int flags;
    const char *name;
    SelectReadProc handler;
    void *handler_data;
    bool exit_on_close;
};

int select_thread(SelectTimerProc callback, void *data) {
    pid_t pid = fork();
    if (pid == 0) { // child
        callback(data);
        _exit(0);
    } // parent...
    return pid;
}

void select_sleep(int msec) {
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 1000*msec;
    select(0,NULL,NULL,NULL,&tv);
}

static void timer_thread(SelectTimer *st) {
    while (1) {
        sleep(st->secs);
        pipe_write(st->tpipe,&st->id,1);
    }
}

static Pipe *timer_pipe(Select *s) {
    return s->tpipe;
}

static void Select_dispose(Select *s) {
    obj_unref(s->fds);
    if (s->timers) {
        obj_unref(timer_pipe(s));
        // this stops the timers from removing themselves from the list!
        List *timers = s->timers;
        s->timers = NULL;
        obj_unref(timers);
    }
}

Select *select_new() {
    Select *me = obj_new(Select,Select_dispose);
    me->nfds = 0;
    me->fds = list_new_ref();
    me->timers = NULL;
    me->p_timeval = NULL;
    me->milli_timer = NULL;
    return me;
}

void SelectFile_dispose(SelectFile *sf) {
    if (sf->name)
        obj_unref(sf->name);
    //printf("disposing %d\n",sf->fd);
}

#define SFILE(item) ((SelectFile*)((item)->data))

SelectFile *select_add_read(Select *s, int fd) {
    SelectFile *sf;
    FOR_LIST_T(SelectFile*,sf,s->fds) {
        if (sf->fd == fd)
            return sf;
    }

    sf = obj_new(SelectFile,SelectFile_dispose);
    sf->fd = fd;
    sf->name = NULL;
    sf->handler = NULL;
    sf->handler_data = NULL;
    sf->exit_on_close = false;
    list_add(s->fds,sf);
    if (fd >= s->nfds)
        s->nfds = fd + 1;
    return sf;
}

static int select_open_file(const char *str, int flags) {
    int oflags = 0;
    if (flags & SelectReadWrite)
        oflags = O_RDWR;
    else if (flags & SelectRead)
        oflags = O_RDONLY;
    else if (flags & SelectWrite)
        oflags = O_WRONLY;
    if (flags & SelectNonBlock)
         oflags |= O_NONBLOCK;

    return open(str,oflags);
}

int select_open(Select *s, const char *str, int flags) {
    int fd = select_open_file(str,flags);
    if (fd == -1) {
        perror("open");
        return -1;
    }
    SelectFile *sf = select_add_read(s, fd);
    sf->name = str_ref(str);
    sf->flags = flags;
    return fd;        
}

int *select_read_fds(Select *s) {
    int *res = array_new(int,list_size(s->fds)), i = 0;
    FOR_LIST(item,s->fds) {
        res[i++] = SFILE(item)->fd;
    }
    return res;
}

#define FOR_LIST_NEXT(var,list) for (ListIter var = (list)->first, _next=var->_next; \
var != NULL; var=_next,_next=var ? var->_next : NULL)

bool select_remove_read(Select *s, int fd) {
    int mfd = 0;
    FOR_LIST_NEXT(item,s->fds) {
        SelectFile *sf = SFILE(item);
        if (sf->fd == fd) {
            obj_unref(list_delete(s->fds,item));
        } else 
        if (fd > mfd) {
            mfd = fd;
        }
    }
    s->nfds = mfd + 1;
    return true;
}

bool select_can_read(Select *s, int fd) {
    return FD_ISSET(fd,&s->fdset);
}

void select_add_reader(Select *s, int fd, bool close, SelectReadProc reader, void *data) {
    if (fd == 0)
        fd = STDIN_FILENO;
    SelectFile *sf = select_add_read(s,fd);
    sf->handler = reader;
    sf->handler_data = data;
    sf->exit_on_close = close;
}

static void SelectTimer_dispose(SelectTimer *st) {
    // this is the crucial thing!
    kill(st->pid,9);
    // the rest is bookkeeping
    List *timers = st->s->timers;
    if (! timers) // because the select state is shutting down!
        return; 
    ListIter iter = list_find(timers,st);
    if (! iter) {
        printf("cannot find the timer in list?\n");
    } else {
        list_delete(timers,iter);
    }        
}

void select_timer_kill(SelectTimer *st) {
    obj_unref(st);
}

static void Chan_dispose(Pipe *pipe) {
    close(pipe->tread);
    close(pipe->twrite);
}

Pipe *pipe_new() {
     int pipefd[2];
     int res = pipe(pipefd);
    if (res != 0)
        return NULL;
    Pipe *pipe = obj_new(Pipe,Chan_dispose);
    pipe->tread = pipefd[0];
    pipe->twrite = pipefd[1];
    return pipe;    
}

int pipe_write(Pipe *pipe, void *buff, int sz) {
    int n = write(pipe->twrite,buff,sz);
    if (n != 1) {
        fprintf(stderr,"wanted to write %d, wrote %d\n",sz,n);
    }
    return n;
}

int pipe_read(Pipe *pipe, void *buff, int sz) {
    return read(pipe->tread,buff,sz);
}

/// fire a timer `callback` every `secs` seconds, passing it `data`.
SelectTimer *select_add_timer(Select *s, int secs, SelectTimerProc callback, void *data) {
    Pipe *pipe;
    if (s->timers == NULL) { 
        pipe = pipe_new(); // timers need a pipenel
        if (pipe == NULL) {
            perror("timer setup");
            return NULL;
        }
         // and we will watch the read end...
        select_add_read(s,pipe->tread);        
        s->timers = list_new_ref();
        s->tpipe = pipe;
    } else { // timers share the same pipenel
        pipe = timer_pipe(s);
    }
           
    SelectTimer *st = obj_new(SelectTimer,SelectTimer_dispose);
    st->s = s;
    st->callback = callback;
    st->data = data;
    st->secs = secs;
    st->tpipe = pipe;
    st->id = list_size(s->timers);
    st->pid = select_thread((SelectTimerProc)timer_thread,st);
    list_add(s->timers,st);
    return st;
}

bool select_do_later(Select *s, int msecs, SelectTimerProc callback, void *data) {
    if (s->milli_timer != NULL) // we're already waiting...
        return false;
    SelectTimer *st = obj_new(SelectTimer,NULL);
    st->s = s;
    s->milli_timer = st;
    st->callback = callback;
    st->data = data;
    s->p_timeval = obj_new(struct timeval,NULL);
    s->p_timeval->tv_sec = 0; // no seconds
    s->p_timeval->tv_usec =  1000*msecs;  // microseconds    
    return true;
}

static void handle_timeout(Select *s) {
    s->milli_timer->callback(s->milli_timer->data);
    obj_unref(s->milli_timer);
    obj_unref(s->p_timeval);
    s->p_timeval = NULL;
    s->milli_timer = NULL;
}


#define LINE_SIZE 256

int select_select(Select *s) {
    fd_set *fds = &s->fdset;
    s->error = false;
    
top:
    
    FD_ZERO(fds);
    FOR_LIST(item,s->fds) {
        FD_SET(SFILE(item)->fd,fds);
    }
    
    int res = select(s->nfds,fds,NULL,NULL,s->p_timeval);
    if (res < 0) {
        s->error = true;
        return -1;
    }
    
    if (res == 0) { // timeout
        handle_timeout(s);
        goto top;
    }
    
    FOR_LIST(item,s->fds) {
        SelectFile *sf = SFILE(item);
        if (sf->handler && select_can_read(s,sf->fd)) {
            char buff[LINE_SIZE];
            int len = read(sf->fd,buff,LINE_SIZE);
            if (len == 0) { // ctrl+D
                if (sf->exit_on_close) {
                    return -1;
                } else { // otherwise we'll just remove the file and close it
                    close(sf->fd);
                    if (sf->flags & SelectReOpen) { // unless we want to simply re-open it
                        sf->fd = select_open_file(sf->name,sf->flags);
                    } else {
                        select_remove_read(s,sf->fd);
                    }
                    break;
                }
            }
            buff[len-1] = '\0'; // strip \n
            int res = sf->handler(buff,sf->handler_data);
            if (sf->exit_on_close && res != 0)
                return -1;
            goto top;
        }
    }
    
    if (s->timers && list_size(s->timers) > 0 && select_can_read_pipe(s,s->tpipe)) {
        byte id;
        SelectTimer *finis = NULL;
        pipe_read(s->tpipe,&id,1);
        FOR_LIST(item,s->timers) {
            SelectTimer *st = (SelectTimer*)item->data;
            if (id == st->id) {
                if (! st->callback(st->data))
                    finis = st;
            }
        }
        if (finis)
            select_timer_kill(finis);
        goto top;
    }
    return res;
}

bool select_can_read_pipe(Select *s, Pipe *pipe) {
    return select_can_read(s,pipe->tread);
}

int pipe_puts(Pipe *p, const char *str) {
    return pipe_write(p,str,strlen(str)+1);
}

#define LINE_SIZE 256
static char buff[LINE_SIZE];

bool callback(Pipe *P) {
    close(P->twrite);
    for (int i = 0;  i < 2; ++i) {
        int n = pipe_read(P,buff,LINE_SIZE);
        printf("got '%s' (%d)\n",buff,n);
    }
    return true;
}

int main()
{
    Pipe *P = pipe_new();
   
    int pid =  select_thread((SelectTimerProc)callback,P);
    
    close(P->tread);
    
    printf("here we go\n");
    pipe_puts(P,"hello there!");
    sleep(0);
    pipe_puts(P,"from us all");
    
    wait(pid);
    
    return 0;
    
        
}

