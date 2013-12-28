#ifndef __LLIB_ARG_H
#define __LLIB_ARG_H
#include "str.h"
#include "value.h"

typedef struct ArgFlags_ {
    const char *spec;
    char alias;
    void *flagptr;
    const char *help;
} ArgFlags;

typedef struct ArgState_ {
    SMap cmd_map;
    void *cmds;
    bool has_commands;
    const char *error;
} ArgState;

ArgState *args_parse_spec(ArgFlags *flagspec);
void arg_functions_as_commands(ArgState *cmds);
void cmd_get_values(PValue *vals,...);
PValue args_process(ArgState *cmds,  const char **argv);
ArgState *args_command_line(ArgFlags *argspec, const char **argv);

#endif
