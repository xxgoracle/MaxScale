/**
 * @file Simple dummy configuration program for non-C++ tests
 * - Configure Maxscale (prepare maxscale.cnf and copy it to Maxscale machine)
 * - check backends
 * - try to restore broken backends
 */


#include <iostream>
#include <unistd.h>
#include "testconnections.h"

using namespace std;

int main(int argc, char *argv[])
{
    if (argc < 3)
    {
        return 1;
    }


    std::string sys =
            std::string(test_dir) +
            std::string("/") +
            std::string(argv[2]) +
            std::string("1 ") +
            std::string(argv[1]);

    int local_argc = argc - 1;
    char** local_argv = &argv[1];

    TestConnections * Test = new TestConnections(local_argc, local_argv);
    (void)Test;
    sleep(3);


    printf("sys=%s\n", sys.c_str());
    fflush(stdout);
    Test->add_result(system(sys.c_str()), "Test %s FAILED!", argv[1]);

    int rval = Test->global_result;
    delete Test;
    return rval;
}
