/**
 * @file clustrix_mon.cpp - simple Clustrix monitor test
 */

#include "testconnections.h"

int main(int argc, char* argv[])
{
    int i;
    TestConnections* Test = new TestConnections(argc, argv);

    int rval = Test->global_result;
    delete Test;
    return rval;
}
