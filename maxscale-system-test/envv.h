#ifndef ENVV_H
#define ENVV_H

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>

char * readenv(const char * name, const char *format, ...);
int readenv_int(const char * name, int def);
bool readenv_bool(const char * name, bool def);

#endif // ENVV_H
