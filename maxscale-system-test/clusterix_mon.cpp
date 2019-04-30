/**
 * @file clusterix_mon.cpp - simple Clusterix monitor test
 */


#include "testconnections.h"


int main(int argc, char* argv[])
{
    TestConnections* Test = new TestConnections(argc, argv);

    Test->clusterix->install_clusterix(0);

    int rval = Test->global_result;
    delete Test;
    return rval;
}
