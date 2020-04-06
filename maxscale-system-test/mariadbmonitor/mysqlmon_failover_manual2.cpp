/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-03-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "testconnections.h"
#include <exception>
#include <iostream>
#include <sstream>
#include <string>

using std::cerr;
using std::cout;
using std::flush;
using std::endl;
using std::string;
using std::stringstream;

namespace
{

namespace x
{

void connect_maxscale(TestConnections& test)
{
    if (test.maxscales->connect_maxscale(0) != 0)
    {
        ++test.global_result;
        throw std::runtime_error("Could not connect to MaxScale.");
    }
}

void try_query(TestConnections& test, const char* zQuery)
{
    if (test.try_query(test.maxscales->conn_rwsplit[0], "%s", zQuery) != 0)
    {
        string s("Could not execute query: ");
        s += zQuery;

        throw std::runtime_error(s);
    }
}

void try_query(TestConnections& test, const std::string& query)
{
    try_query(test, query.c_str());
}

void stop_node(Mariadb_nodes& nodes, int node)
{
    if (nodes.stop_node(node) != 0)
    {
        throw std::runtime_error("Could not stop node.");
    }
}

void fail_query(TestConnections& test)
{
    int rv = execute_query(test.maxscales->conn_rwsplit[0], "BEGIN");

    if (rv == 0)
    {
        const char MESSAGE[] = "A query that was expected to fail, did not fail.";

        test.add_result(false, "%s", MESSAGE);
        throw std::runtime_error(MESSAGE);
    }
}
}
}

namespace
{

void list_servers(TestConnections& test)
{
    test.maxscales->execute_maxadmin_command_print(0, (char*)"list servers");
}

void create_table(TestConnections& test)
{
    x::try_query(test, "DROP TABLE IF EXISTS test.t1");
    x::try_query(test, "CREATE TABLE test.t1(id INT)");
}

void insert_data(TestConnections& test)
{
    x::try_query(test, "BEGIN");
    for (int i = 0; i < 20; ++i)
    {
        stringstream ss;
        ss << "INSERT INTO test.t1 VALUES (" << i << ")";
        x::try_query(test, ss.str());
    }
    x::try_query(test, "COMMIT");
}

void run(TestConnections& test)
{
    cout << "\nConnecting to MaxScale." << endl;
    x::connect_maxscale(test);

    cout << "\nCreating table." << endl;
    create_table(test);

    cout << "\nInserting data." << endl;
    insert_data(test);

    list_servers(test);

    cout << "\nSyncing slaves." << endl;
    test.repl->sync_slaves();

    cout << "\nStopping master." << endl;
    x::stop_node(*test.repl, 0);

    list_servers(test);

    cout << "\nShould fail as master is no longer available, but trying to execute a query... " << endl;
    x::fail_query(test);
    cout << "Failed as expected." << endl;

    list_servers(test);

    cout << "\nPerforming failover... " << endl;
    test.maxscales->wait_for_monitor();
    test.maxscales->execute_maxadmin_command_print(0, (char*)"call command mysqlmon failover MySQL-Monitor");

    list_servers(test);

    cout << "\nShould still fail as there is not transparent master failover, "
         << "but trying to execute a query... " << endl;
    x::fail_query(test);
    cout << "Failed as expected." << endl;

    cout << "\nClosing connection to MaxScale." << endl;
    test.maxscales->close_maxscale_connections(0);
    test.maxscales->wait_for_monitor();

    cout << "\nConnecting to MaxScale." << endl;
    x::connect_maxscale(test);

    list_servers(test);

    cout << "Trying to insert data... " << flush;
    insert_data(test);
    cout << "succeeded." << endl;
    x::try_query(test, "DROP TABLE test.t1");
}
}

int main(int argc, char** argv)
{
    Mariadb_nodes::require_gtid(true);
    TestConnections test(argc, argv);

    try
    {
        run(test);
    }
    catch (const std::exception& x)
    {
        cerr << "error: Execution was terminated due to an exception: " << x.what() << endl;
    }

    return test.global_result;
}
