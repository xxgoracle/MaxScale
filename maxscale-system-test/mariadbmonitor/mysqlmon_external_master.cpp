/**
 * Test monitoring and failover with ignore_external_masters=true
 */
#include "testconnections.h"
#include "fail_switch_rejoin_common.cpp"
#include <atomic>
#include <thread>

const char DOWN[] = "Down";
const char RUNNING[] = "Running";
const char MASTER[] = "Master";
const char SLAVE[] = "Slave";

const StringSet master_running = {MASTER, RUNNING};
const StringSet slave_running = {SLAVE, RUNNING};
const StringSet running = {RUNNING};
const StringSet down = {DOWN};

static std::atomic<bool> is_running(true);

void check_status(TestConnections& test, const char* server, const StringSet& expected, const char* message)
{
    StringSet state = test.get_server_status(server);
    test.expect(state == expected, "%s: %s", message, dump_status(state, expected).c_str());
}

void writer_func(TestConnections* test)
{
    while (is_running)
    {
        MYSQL* conn = open_conn(test->maxscales->rwsplit_port[0], test->maxscales->IP[0],
                                "test", "test", false);

        for (int i = 0; i < 100; i++)
        {
            if (execute_query_silent(conn, "INSERT INTO test.t1 VALUES (SELECT SLEEP(0.5))"))
            {
                sleep(1);
                break;
            }
        }
        mysql_close(conn);
    }
}

int main(int argc, char** argv)
{
    Mariadb_nodes::require_gtid(true);
    TestConnections test(argc, argv);
    test.repl->connect();
    delete_slave_binlogs(test);

    // Create a table and a user and start a thread that does writes
    MYSQL* node0 = test.repl->nodes[0];
    execute_query(node0, "CREATE OR REPLACE TABLE test.t1 (id INT)");
    execute_query(node0, "DROP USER IF EXISTS 'test'@'%%'");
    execute_query(node0, "CREATE USER 'test'@'%%' IDENTIFIED BY 'test'");
    execute_query(node0, "GRANT INSERT, SELECT, UPDATE, DELETE ON *.* TO 'test'@'%%'");
    test.repl->sync_slaves();
    std::thread thr(writer_func, &test);

    test.tprintf("Start by having the current master replicate from the external server.");
    test.repl->replicate_from(0, 3);
    test.maxscales->wait_for_monitor(1);
    check_status(test, "server1", master_running, "server1 should be the master");
    check_status(test, "server2", slave_running, "server2 should be a slave");
    check_status(test, "server3", slave_running, "server3 should be a slave");

    test.tprintf("Stop server1, expect server2 to be promoted as the master");
    test.repl->stop_node(0);
    test.maxscales->wait_for_monitor(2);

    check_status(test, "server1", down, "server1 should be down");
    check_status(test, "server2", master_running, "server2 should be the master");
    check_status(test, "server3", slave_running, "server3 should be a slave");

    test.tprintf("Configure master-master replication between server2 and the external server");
    // Comment away next line since failover already created the external connection. Failover/switchover
    // does not respect 'ignore_external_master' when copying slave connections. Whether it should do it
    // is questionable.
    // TODO: Think about what to do with this test and the setting in general.
    //    test.repl->replicate_from(1, 3);
    test.repl->replicate_from(3, 1);
    test.maxscales->wait_for_monitor(1);
    check_status(test, "server2", master_running, "server2 should still be the master");
    check_status(test, "server3", slave_running, "server3 should be a slave");

    test.tprintf("Start server1, expect it to rejoin the cluster");
    // The rejoin should redirect the existing external master connection in server1.
    test.repl->start_node(0);
    test.maxscales->wait_for_monitor(2);
    check_status(test, "server1", slave_running, "server1 should be a slave");
    check_status(test, "server2", master_running, "server2 should still be the master");
    check_status(test, "server3", slave_running, "server3 should be a slave");

    test.tprintf("Stop server2, expect server1 to be promoted as the master");
    test.repl->stop_node(1);
    test.maxscales->wait_for_monitor(2);
    test.repl->connect();
    // Same as before.
    // test.repl->replicate_from(0, 3);
    test.repl->replicate_from(3, 0);

    check_status(test, "server1", master_running, "server1 should be the master");
    check_status(test, "server2", down, "server2 should be down");
    check_status(test, "server3", slave_running, "server3 should be a slave");

    test.tprintf("Start server2, expect it to rejoin the cluster");
    test.repl->start_node(1);
    test.maxscales->wait_for_monitor(2);
    check_status(test, "server1", master_running, "server1 should still be the master");
    check_status(test, "server2", slave_running, "server2 should be a slave");
    check_status(test, "server3", slave_running, "server3 should be a slave");

    // Cleanup
    is_running = false;
    thr.join();
    execute_query(test.repl->nodes[0], "STOP SLAVE; RESET SLAVE ALL;");

    return test.global_result;
}
