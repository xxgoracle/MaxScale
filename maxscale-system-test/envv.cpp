#include <string.h>
#include <string>
#include "envv.h"

char * readenv(const char * name, const char *format, ...)
{
    char * env = getenv(name);
    if (!env)
    {
        va_list argp;
        va_start(argp, format);
        //int i = snprintf(NULL, 0, format, argp);
        env = (char *) malloc(4096); //TODO calculate needed memory
        vprintf(format, argp);
        vsprintf(env, format, argp);
        va_end(argp);
        setenv(name, env, 1);
    }
    return env;
}

int readenv_int(const char * name, int def)
{
    int x;
    char * env = getenv(name);
    if (env)
    {
        sscanf(env, "%d", &x);
    }
    else
    {
        x = def;
        setenv(name, (std::to_string(x).c_str()), 1);
    }
    return x;
}

bool readenv_bool(const char * name, bool def)
{
    char * env = getenv(name);
    if (env)
    {
        return ((strcasecmp(env, "yes") == 0) ||
                (strcasecmp(env, "true") == 0));
    }
    else
    {
        setenv(name, def ? "true":"false", 1);
        return def;
    }
}
