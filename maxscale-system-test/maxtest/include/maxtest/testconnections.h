#pragma once

#include "mariadb_nodes.h"
#include "maxscales.h"
#include "test_dir.hh"
#include <fcntl.h>
#include <pthread.h>
#include <sys/time.h>
#include <set>
#include <string>
#include <vector>
#include <thread>
#include <functional>

#include <maxbase/ccdefs.hh>

typedef std::set<std::string> StringSet;

/**
 * @brief Class contains references to Master/Slave and Galera test setups
 * Test setup should consist of two setups: one Master/Slave and one Galera.
 *
 * Maxscale should be configured separatelly for every test.
 *
 * Test setup should be described by enviromental variables:
 * - Maxscale_IP - IP adress of Maxscale machine
 * - Maxscale_User - User name to access Maxscale services
 * - Maxscale_Password - Password to access Maxscale services
 * - Maxscale_sshkey - ssh key for Maxscale machine
 * - maxscale_cnf - name of maxscale .cnf file (full)
 * - KillVMCommand - Command to kill a node (should handle one parameter: IP address of virtual machine to
 * kill)
 * - StartVMCommand - Command to restart virtual machine (should handle one parameter: IP address of virtual
 * machine to kill)
 * - GetLogsCommand - Command to copy log files from node virtual machines (should handle one parameter: IP
 * address of virtual machine to kill)
 * - SysbenchDir - path to SysBench directory (sysbanch should be >= 0.5)
 * - node_N - Number of Master/Slave setup nodes
 * - node_NNN - IP address of node NNN (NNN - 3 digits node index starting from 000)
 * - node_port_NNN - MariaDB port for node NNN
 * - node_sshkey_NNN - ssh key to access node NNN (should be sutable for 'root' and 'ec2-user')
 * - node_User - User name to access Master/Slav setup
 * - node_Password - Password to access Master/Slave setup
 * - galera_N, galera_NNN, galera_port_NNN, galera_sshkey_NNN, galera_User, galera_Password - same for Galera
 * setup
 *
 */
class TestConnections
{
public:
    using StringSet = std::set<std::string>;

    /**
     * @brief TestConnections constructor: reads environmental variables, copies MaxScale.cnf for MaxScale
     * machine
     * @param test_exec_name Path to currect executable
     */
    TestConnections(int argc, char* argv[]);

    ~TestConnections();

    /**
     * @brief Is the test still ok?
     *
     * @return True, if no errors have occurred, false otherwise.
     */
    bool ok() const
    {
        return global_result == 0;
    }

    /**
     * @brief Has the test failed?
     *
     * @return True, if errors have occurred, false otherwise.
     */
    bool failed() const
    {
        return global_result != 0;
    }

    /**
     * @brief global_result Result of test, 0 if PASSED
     */
    int global_result {0};

    /**
     * @brief galera Mariadb_nodes object containing references to Galera setuo
     */
    Galera_nodes* galera {nullptr};

    /**
     * @brief repl Mariadb_nodes object containing references to Master/Slave setuo
     */
    Mariadb_nodes* repl {nullptr};

    /**
     * @brief maxscales Maxscale object containing referebces to all Maxscale machines
     */
    Maxscales* maxscales {nullptr};

    /**
     * @brief copy_mariadb_logs copies MariaDB logs from backend
     * @param repl Mariadb_nodes object
     * @param prefix file name prefix
     * @return 0 if success
     */
    int copy_mariadb_logs(Mariadb_nodes* nrepl, const char* prefix, std::vector<std::thread>& threads);

    /**
     * @brief verbose if true more printing activated
     */
    static bool verbose;

    /**
     * @brief smoke if true all tests are executed in quick mode
     */
    bool smoke {true};

    /**
     * @brief binlog_cmd_option index of mariadb start option
     */
    int binlog_cmd_option {0};

    /**
     * @brief ssl if true ssl will be used
     */
    int ssl {false};

    /**
     * @brief backend_ssl if true ssl configuratio for all servers will be added
     */
    bool backend_ssl {false};

    /**
     * @brief binlog_master_gtid If true start_binlog() function configures Maxscale
     * binlog router to use GTID to connect to Master
     */
    bool binlog_master_gtid {false};

    /**
     * @brief binlog_slave_gtid If true start_binlog() function configures slaves
     * to use GTID to connect to Maxscale binlog router
     */
    bool binlog_slave_gtid {false};

    /**
     * @brief timeout seconds until test termination
     */
    long int timeout;

    /**
     * @brief log_copy_interval seconds between log copying
     */
    long int log_copy_interval;

    /**
     * @brief log_copy_interval seconds until next log copying
     */
    long int log_copy_to_go;

    /**
     * @brief timeout_thread_p pointer to timeout thread
     */
    pthread_t timeout_thread_p;

    /**
     * @brief log_copy_thread_p pointer to log copying thread
     */
    pthread_t log_copy_thread_p;

    /** Check whether all nodes are in a valid state */
    static void check_nodes(bool value);

    /** Skip initial start of MaxScale */
    static void skip_maxscale_start(bool value);

    /** Prepare multiple maxscale instances */
    static void multiple_maxscales(bool value);

    /** Test requires a certain backend version  */
    static void require_repl_version(const char* version);
    static void require_galera_version(const char* version);

    /** Require that galera is present*/
    static void require_galera(bool value);

    /** Require that columnstore is present*/
    static void require_columnstore(bool value);

    /**
     * @brief Specify whether galera should be restarted.
     *
     * @param value If true, galera should be restarted.
     *
     * @note Even if set to false (which is also the default), '-g' or '--restart-galera' at
     *       the command line will still cause a restart, unless '-y' or '--no-galera' has
     *       been specified. '-y' will prevent galera from being restarted even if the value
     *       has been set to true.
     */
    static void restart_galera(bool value);

    /**
     * @brief add_result adds result to global_result and prints error message if result is not 0
     * @param result 0 if step PASSED
     * @param format ... message to pring if result is not 0
     */
    void add_result(bool result, const char* format, ...) __attribute__ ((format(printf, 3, 4)));

    /** Same as add_result() but inverted */
    void expect(bool result, const char* format, ...) __attribute__ ((format(printf, 3, 4)));

    /**
     * @brief read_mdbci_info Reads name of MDBCI config and tryes to load all network info
     */
    void read_mdbci_info();

    /**
     * @brief ReadEnv Reads all Maxscale and Master/Slave and Galera setups info from environmental variables
     */
    void read_env();

    /**
     * @brief PrintIP   Prints all Maxscale and Master/Slave and Galera setups info
     */
    void print_env();

    /**
     * @brief InitMaxscale  Copies MaxSclae.cnf and start MaxScale
     * @param m Number of Maxscale node
     */
    void init_maxscale(int m = 0);

    /**
     * @brief InitMaxscale  Copies MaxSclae.cnf and start MaxScale on all Maxscale nodes
     */
    void init_maxscales();

    /**
     * @brief start_binlog configure first node as Master, Second as slave connected to Master and others as
     * slave connected to MaxScale binlog router
     * @return  0 in case of success
     */
    int start_binlog(int m = 0);

    /**
     * @brief Start binlogrouter replication from master
     */
    bool replicate_from_master(int m = 0);

    /**
     * @brief Stop binlogrouter replication from master
     */
    void revert_replicate_from_master();

    /**
     * @brief prepare_binlog clean up binlog directory, set proper access rights to it
     * @return 0
     */
    int prepare_binlog(int m = 0);

    /**
     * @brief start_mm configure first node as Master for second, Second as Master for first
     * @return  0 in case of success
     */
    int start_mm(int m = 0);

    /**
     * @brief copy_all_logs Copies all MaxScale logs and (if happens) core to current workspace
     */
    int copy_all_logs();

    /**
     * @brief copy_all_logs_periodic Copies all MaxScale logs and (if happens) core to current workspace and
     * sends time stemp to log copying script
     */
    int copy_all_logs_periodic();

    /**
     * @brief copy_maxscale_logs Copies logs from all Maxscale nodes
     * @param timestamp
     * @return 0
     */
    int copy_maxscale_logs(double timestamp);

    /**
     * @brief Test that connections to MaxScale are in the expected state
     * @param rw_split State of the MaxScale connection to Readwritesplit. True for working connection, false
     * for no connection.
     * @param rc_master State of the MaxScale connection to Readconnroute Master. True for working connection,
     * false for no connection.
     * @param rc_slave State of the MaxScale connection to Readconnroute Slave. True for working connection,
     * false for no connection.
     * @return  0 if connections are in the expected state
     */
    int test_maxscale_connections(int m,
                                  bool rw_split,
                                  bool rc_master,
                                  bool rc_slave);

    /**
     * @brief Create a number of connections to all services, run simple query, close all connections
     * @param conn_N number of connections
     * @param rwsplit_flag if true connections to RWSplit router will be created, if false - no connections to
     * RWSplit
     * @param master_flag if true connections to ReadConn master router will be created, if false - no
     * connections to ReadConn master
     * @param slave_flag if true connections to ReadConn slave router will be created, if false - no
     * connections to ReadConn slave
     * @param galera_flag if true connections to RWSplit router with Galera backend will be created, if false
     *- no connections to RWSplit with Galera backend
     * @return  0 in case of success
     */
    int create_connections(int m,
                           int conn_N,
                           bool rwsplit_flag,
                           bool master_flag,
                           bool slave_flag,
                           bool galera_flag);

    /**
     * Trying to get client IP address by connection to DB via RWSplit and execution 'show processlist'
     *
     * @param ip client IP address as it visible by Maxscale
     * @return 0 in case of success
     */
    int get_client_ip(int m, char* ip);

    /**
     * @brief set_timeout startes timeout thread which terminates test application after timeout_seconds
     * @param timeout_seconds timeout time
     * @return 0 if success
     */
    int set_timeout(long int timeout_seconds);

    /**
     * @brief set_log_copy_interval sets interval for periodic log copying
     * @param interval_seconds interval in seconds
     * @return 0 if success
     */
    int set_log_copy_interval(long int interval_seconds);

    /**
     * @brief stop_timeout stops timeout thread
     * @return 0
     */
    int stop_timeout();

    /**
     * @brief printf with automatic timestamps
     */
    void tprintf(const char* format, ...);

    /**
     * @brief Creats t1 table, insert data into it and checks if data can be correctly read from all Maxscale
     * services
     * @param Test Pointer to TestConnections object that contains references to test setup
     * @param N number of INSERTs; every next INSERT is longer 16 times in compare with previous one: for N=4
     * last INSERT is about 700kb long
     * @return 0 in case of no error and all checks are ok
     */
    int insert_select(int m, int N);

    /**
     * @brief Executes USE command for all Maxscale service and all Master/Slave backend nodes
     * @param Test Pointer to TestConnections object that contains references to test setup
     * @param db Name of DB in 'USE' command
     * @return 0 in case of success
     */
    int use_db(int m, char* db);

    /**
     * @brief Checks if table t1 exists in DB
     * @param presence expected result
     * @param db DB name
     * @return 0 if (t1 table exists AND presence=TRUE) OR (t1 table does not exist AND presence=false)
     */

    int check_t1_table(int m, bool presence, char* db);

    /**
     * @brief Check whether logs match a pattern
     *
     * The patterns are interpreted as `grep` compatible patterns (BRE regular expressions). If the
     * log file does not match the pattern, it is considered an error.
     */
    void log_includes(int m, const char* pattern);

    /**
     * @brief Check whether logs do not match a pattern
     *
     * The patterns are interpreted as `grep` compatible patterns (BRE regular expressions). If the
     * log file match the pattern, it is considered an error.
     */
    void log_excludes(int m, const char* pattern);

    /**
     * @brief FindConnectedSlave Finds slave node which has connections from MaxScale
     * @param Test TestConnections object which contains info about test setup
     * @param global_result pointer to variable which is increased in case of error
     * @return index of found slave node
     */
    int find_connected_slave(int m, int* global_result);

    /**
     * @brief FindConnectedSlave1 same as FindConnectedSlave() but does not increase global_result
     * @param Test  TestConnections object which contains info about test setup
     * @return index of found slave node
     */
    int find_connected_slave1(int m = 0);

    /**
     * @brief CheckMaxscaleAlive Checks if MaxScale is alive
     * Reads test setup info from enviromental variables and tries to connect to all Maxscale services to
     * check if i is alive.
     * Also 'show processlist' query is executed using all services
     * @return 0 in case if success
     */
    int check_maxscale_alive(int m = 0);

    /**
     * @brief try_query Executes SQL query and repors error
     * @param conn MYSQL struct
     * @param sql SQL string
     * @return 0 if ok
     */
    int try_query(MYSQL* conn, const char* sql, ...) mxb_attribute((format(printf, 3, 4)));

    /**
     * @brief try_query_all Executes SQL query on all MaxScale connections
     * @param sql SQL string
     * @return 0 if ok
     */
    int try_query_all(int m, const char* sql);

    /**
     * @brief Get the set of labels that are assigned to server @c name
     *
     * @param name The name of the server that must be present in the output `maxadmin list servers`
     *
     * @return A set of string labels assigned to this server
     */
    StringSet get_server_status(const char* name);

    /**
     * @brief check_maxscale_processes Check if number of running Maxscale processes is equal to 'expected'
     * @param expected expected number of Maxscale processes
     * @return 0 if check is done
     */
    int check_maxscale_processes(int m, int expected);

    /**
     * @brief list_dirs Execute 'ls' on binlog directory on all repl nodes and on Maxscale node
     * @return 0
     */
    int list_dirs(int m = 0);

    /**
     * @brief make_snapshot Makes a snapshot for all running VMs
     * @param snapshot_name name of created snapshot
     * @return 0 in case of success or mdbci error code in case of error
     */
    int take_snapshot(char* snapshot_name);

    /**
     * @brief revert_snapshot Revert snapshot for all running VMs
     * @param snapshot_name name of snapshot to revert
     * @return 0 in case of success or mdbci error code in case of error
     */
    int revert_snapshot(char* snapshot_name);

    /**
     * @brief Test a bad configuration
     * @param config Name of the config template
     * @return Always false, the test will time out if the loading is successful
     */
    bool test_bad_config(int m, const std::string& config);

    /**
     * @brief Process a template configuration file
     *
     * @param cnf_template_path Config file template path
     * @param dest Destination file name for actual configuration file
     */
    void process_template(int m, const std::string& cnf_template_path,
                          const char* dest = "/etc/maxscale.cnf");

    /**
     * Execute a MaxCtrl command
     *
     * @param cmd  Command to execute, without the `maxctrl` part
     * @param m    MaxScale node to execute the command on
     * @param sudo Run the command as root
     *
     * @return The exit code and output of MaxCtrl
     */
    std::pair<int, std::string> maxctrl(std::string cmd, int m = 0, bool sudo = true)
    {
        return maxscales->ssh_output("maxctrl " + cmd, m, sudo);
    }

    void check_maxctrl(std::string cmd, int m = 0, bool sudo = true)
    {
        auto result = maxctrl(cmd, m, sudo);
        expect(result.first == 0, "Command '%s' should work: %s", cmd.c_str(), result.second.c_str());
    }

    void check_current_operations(int m, int value);
    void check_current_connections(int m, int value);
    int  stop_maxscale(int m = 0);
    int  start_maxscale(int m = 0);

    /**
     * Get the current master server id from the cluster, as seen by rwsplit.
     *
     * @param m MaxScale node index
     * @return Server id of the master
     */
    int get_master_server_id(int m = 0);

    /**
     * Add a callback that is called when the test ends
     *
     * @param func Function to call
     */
    void on_destroy(std::function<void(void)> func)
    {
        m_on_destroy.push_back(func);
    }

    /**
     * @brief process_mdbci_template Read template file from maxscale-system-test/mdbci/templates
     * and replace all placeholders with acutal values
     * @return 0 in case of success
     */
    int process_mdbci_template();

    /**
     * @brief call_mdbci Execute MDBCI to bring up nodes
     * @return 0 if success
     */
    int call_mdbci(const char* options);

    /**
     * @brief resinstall_maxscales Remove Maxscale form all nodes and installs new ones
     * (to be used for run_test_snapshot)
     * @return 0 in case of success
     */
    int reinstall_maxscales();

private:
    void report_result(const char* format, va_list argp);
    void copy_one_mariadb_log(Mariadb_nodes* nrepl, int i, std::string filename);

    void set_template_and_labels();
    void set_mdbci_labels();
    bool log_matches(int m, const char* pattern);

    bool too_many_maxscales() const;

    std::vector<std::function<void(void)>> m_on_destroy;

    std::string m_test_name;            /**< Test name */
    std::string m_cnf_template_path;    /**< MaxScale config file template used by test */

    std::string m_test_labels_str;  /**< Test labels as given in CMakeLists.txt and required by the test */
    StringSet   m_test_labels;      /**< Test labels parsed to a set. */

    StringSet   m_required_mdbci_labels;/**< MDBCI-labels required by test. Subset of test labels. */
    std::string m_mdbci_labels_str;     /**< MDBCI-labels in string form. Used on the command line. */

    StringSet m_configured_mdbci_labels;    /**< MDBCI-labels already configured on the VM setup */

    std::string m_mdbci_config_name;    /**< Name of MDBCI VMs set */
    std::string m_mdbci_vm_path;        /**< Path to directory with MDBCI VMs descriptions */
    std::string m_mdbci_template;       /**< Name of mdbci VMs template file */
    std::string m_target;               /**< Name of Maxscale repository in the CI */
    std::string m_network_config;       /**< Content of MDBCI network_config file */
    std::string m_vm_path;              /**< Path to the VM Vagrant directory */

    /**
     * Command to copy log files from node virtual machines (should handle one parameter: IP address of
     * virtual machine to kill) */
    std::string m_get_logs_command;

    std::string m_take_snapshot_command;    /**< Command line to create a snapshot of all VMs */
    std::string m_revert_snapshot_command;  /**< Command line to revert a snapshot of all VMs */

    std::string m_ssl_options;       /**< String with ssl configuration for command line client */

    bool m_enable_timeouts {true};      /**< Whether timeouts are enabled or not */
    bool m_local_maxscale {false};      /**< MaxScale runs locally, specified using -l. */

    /**< If true, every test is trying to revert snapshot before running the test. Unused for now. */
    bool m_use_snapshots {false};

    /* If true, logs from backends are not copied (needed if case of Aurora RDS backend or similar) */
    bool m_no_backend_log_copy {false};
    bool m_no_maxscale_log_copy {false};    /**< Do not download MaxScale logs. */

    bool no_repl {false};   /**< Do not check, restart and use Master/Slave setup */
    bool no_galera {false}; /**< Do not check, restart and use Galera setup; all Galera tests will fail */

    /** If true tests do not revert VMs after the test even if test failed (use it for debugging) */
    bool no_vm_revert {true};

    int m_threads {4};      /**< Number of Maxscale threads */

    timeval m_start_time {0, 0};    /**< time when test was started (used by printf to print Timestamp) */

    /**
     * If true IPv6 addresses will be used to connect Maxscale and backed Also IPv6 addresses go to
     * maxscale.cnf. */
    bool m_use_ipv6 {false};

    /**
     * Flag that is set when 'reinstall_maxscale'-option is provided. If true, Maxscale will be removed
     * and re-installed on all Maxscale nodes. Used for 'run_test_snapshot'.
     */
    bool m_reinstall_maxscale {false};

    std::string flatten_stringset(const StringSet& set);
    StringSet   parse_to_stringset(const std::string& source);
};

/**
 * @brief timeout_thread Thread which terminates test application after 'timeout' milliseconds
 * @param ptr pointer to TestConnections object
 * @return void
 */
void* timeout_thread(void* ptr);

/**
 * @brief log_copy_thread Thread which peridically copies logs from Maxscale machine
 * @param ptr pointer to TestConnections object
 * @return void
 */
void* log_copy_thread(void* ptr);

/**
 * Dump two server status sets as strings
 *
 * @param current  The current status
 * @param expected The expected status
 *
 * @return String form comparison of status sets
 */
std::string dump_status(const StringSet& current, const StringSet& expected);
