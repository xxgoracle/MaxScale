/**
 * @file support-209_failover_readonly
 */

#include <iostream>
#include "testconnections.h"
#include "sql_t1.h"

using namespace std;

bool exit_flag = false;

void *query_thread(void *ptr);
TestConnections * Test ;

int main(int argc, char *argv[])
{

    int ec;
    Test = new TestConnections(argc, argv);

    Test->repl->N = 2;
    Test->repl->require_gtid(true);
    Test->repl->start_replication();
    sleep(20);
    Test->repl->connect();
    execute_query(Test->repl->nodes[0], "DROP TABLE IF EXISTS t1;");
    create_t1(Test->repl->nodes[0]);
    execute_query(Test->repl->nodes[0], (char *) "INSERT INTO t1 VALUES (111, 222)");
    Test->repl->close_connections();

    pthread_t thread;
    pthread_create(&thread, NULL, query_thread, NULL);

    printf("%s\n", Test->maxscales->ssh_node_output(0, "maxadmin -h 127.0.0.1 -P 6603 -uadmin -pmariadb show servers", false, &ec));
    printf("node 0: \n %s\n", Test->repl->ssh_node_output(0, "echo \"show variables like \\\"read_only\\\"\" | sudo mysql ", true, &ec));
    printf("node 1: \n %s\n", Test->repl->ssh_node_output(1, "echo \"show variables like \\\"read_only\\\"\" | sudo mysql ", true, &ec));
    Test->tprintf("killing master\n");
    Test->repl->ssh_node(0, "pid=`pgrep -f mysql`; kill $pid", true);
    sleep(100);
    printf("%s\n", Test->maxscales->ssh_node_output(0, "maxadmin -h 127.0.0.1 -P 6603 -uadmin -pmariadb show servers", false, &ec));
    printf("node 1: \n %s\n", Test->repl->ssh_node_output(1, "echo \"show variables like \\\"read_only\\\"\" | sudo mysql ", true, &ec));
    Test->tprintf("starting node 0\n");
    Test->repl->start_node(0, "");

    Test->tprintf("node 0 restarted\n");
    sleep(60);
    printf("%s\n", Test->maxscales->ssh_node_output(0, "maxadmin -h 127.0.0.1 -P 6603 -uadmin -pmariadb show servers", false, &ec));
    printf("node 0: \n %s\n", Test->repl->ssh_node_output(0, "echo \"show variables like \\\"read_only\\\"\" | sudo mysql ", true, &ec));
    printf("node 1: \n %s\n", Test->repl->ssh_node_output(1, "echo \"show variables like \\\"read_only\\\"\" | sudo mysql ", true, &ec));
    Test->tprintf("Wating 29 minutes more like it described in the support case\n");
    sleep(29*60);
    Test->tprintf("killing node0 again\n");
    Test->repl->ssh_node(0, "pid=`pgrep -f mysql`; kill $pid", true);
    sleep(100);
    printf("%s\n", Test->maxscales->ssh_node_output(0, "maxadmin -h 127.0.0.1 -P 6603 -uadmin -pmariadb show servers", false, &ec));
    printf("node 1: \n %s\n", Test->repl->ssh_node_output(1, "echo \"show variables like \\\"read_only\\\"\" | sudo mysql ", true, &ec));
    Test->tprintf("starting node 0\n");
    Test->repl->start_node(0, "");
    Test->tprintf("node 0 restarted\n");
    sleep(100);
    printf("%s\n", Test->maxscales->ssh_node_output(0, "maxadmin -h 127.0.0.1 -P 6603 -uadmin -pmariadb show servers", false, &ec));
    printf("node 0: \n %s\n", Test->repl->ssh_node_output(0, "echo \"show variables like \\\"read_only\\\"\" | sudo mysql ", true, &ec));
    printf("node 1: \n %s\n", Test->repl->ssh_node_output(1, "echo \"show variables like \\\"read_only\\\"\" | sudo mysql ", true, &ec));


    exit_flag = true;
    pthread_join(thread, NULL);

    Test->check_maxscale_alive(0);

    int rval = Test->global_result;
    delete Test;
    return rval;
}

void *query_thread(void *ptr)
{
    Test->maxscales->verbose = true;
    Test->maxscales->connect_maxscale(0);
    while (!exit_flag)
    {
        char str[256];
        sprintf(str, "INSERT INTO t1 VALUES (%d, %d)", 11, 22);
        //execute_query(Test->maxscales->conn_rwsplit[0], str);
    }

    Test->maxscales->close_maxscale_connections(0);
    return NULL;
}
