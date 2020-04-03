#include <getopt.h>
#include <libgen.h>
#include <pthread.h>
#include <stdarg.h>
#include <sys/time.h>
#include <time.h>
#include <signal.h>
#include <execinfo.h>
#include <sys/stat.h>
#include <iostream>
#include <sstream>
#include <string>
#include <fstream>
#include <iostream>
#include <future>
#include <maxbase/stacktrace.hh>
#include <algorithm>

#include "mariadb_func.h"
#include "maxadmin_operations.h"
#include "sql_t1.h"
#include "testconnections.h"
#include "test_info.hh"
#include "envv.h"

using namespace mxb;
using std::cout;
using std::endl;
using std::string;

namespace
{
// These must match the labels recognized by MDBCI.
const string label_repl_be = "REPL_BACKEND";
const string label_galera_be = "GALERA_BACKEND";
const string label_big_be = "BIG_REPL_BACKEND";
const string label_2nd_mxs = "SECOND_MAXSCALE";
const string label_cs_be = "COLUMNSTORE_BACKEND";

const StringSet recognized_mdbci_labels =
{label_repl_be, label_big_be, label_galera_be, label_2nd_mxs, label_cs_be};

const int MDBCI_FAIL = 200;     // Exit code when failure caused by MDBCI non-zero exit
const int BROKEN_VM_FAIL = 201; // Exit code when failure caused by broken VMs
}

namespace maxscale
{

static bool start = true;
static bool check_nodes = true;
static bool manual_debug = false;
static std::string required_repl_version;
static std::string required_galera_version;
static bool restart_galera = false;
static bool require_galera = false;
static bool require_columnstore = false;
static bool multiple_maxscales = false;
}

static void perform_manual_action(const char* zMessage)
{
    std::cout << zMessage << " (press enter when done)." << std::endl;
    std::string not_used;
    std::getline(std::cin, not_used);
    std::cout << "Ok" << std::endl;
}

static void signal_set(int sig, void (* handler)(int))
{
    struct sigaction sigact = {};
    sigact.sa_handler = handler;

    do
    {
        errno = 0;
        sigaction(sig, &sigact, NULL);
    }
    while (errno == EINTR);
}

void sigfatal_handler(int i)
{
    dump_stacktrace();
    signal_set(i, SIG_DFL);
    raise(i);
}

void TestConnections::check_nodes(bool value)
{
    maxscale::check_nodes = value;
}

void TestConnections::skip_maxscale_start(bool value)
{
    maxscale::start = !value;
}

void TestConnections::multiple_maxscales(bool value)
{
    maxscale::multiple_maxscales = value;
}

void TestConnections::require_repl_version(const char* version)
{
    maxscale::required_repl_version = version;
}

void TestConnections::require_galera_version(const char* version)
{
    maxscale::required_galera_version = version;
}

void TestConnections::require_galera(bool value)
{
    maxscale::require_galera = value;
}

void TestConnections::require_columnstore(bool value)
{
    maxscale::require_columnstore = value;
}

void TestConnections::restart_galera(bool value)
{
    maxscale::restart_galera = value;
}

bool TestConnections::verbose = false;

TestConnections::TestConnections(int argc, char* argv[])
{
    std::ios::sync_with_stdio(true);
    signal_set(SIGSEGV, sigfatal_handler);
    signal_set(SIGABRT, sigfatal_handler);
    signal_set(SIGFPE, sigfatal_handler);
    signal_set(SIGILL, sigfatal_handler);
#ifdef SIGBUS
    signal_set(SIGBUS, sigfatal_handler);
#endif
    gettimeofday(&m_start_time, NULL);

    read_env();

    bool maxscale_init = true;

    static struct option long_options[] =
    {

        {"help",               no_argument,       0, 'h'},
        {"verbose",            no_argument,       0, 'v'},
        {"silent",             no_argument,       0, 'n'},
        {"quiet",              no_argument,       0, 'q'},
        {"no-maxscale-start",  no_argument,       0, 's'},
        {"no-maxscale-init",   no_argument,       0, 'i'},
        {"no-nodes-check",     no_argument,       0, 'r'},
        {"restart-galera",     no_argument,       0, 'g'},
        {"no-timeouts",        no_argument,       0, 'z'},
        {"no-galera",          no_argument,       0, 'y'},
        {"local-maxscale",     optional_argument, 0, 'l'},
        {"reinstall-maxscale", no_argument,       0, 'm'},
        {0,                    0,                 0, 0  }
    };

    int c;
    int option_index = 0;

    while ((c = getopt_long(argc, argv, "hvnqsirgzyl::", long_options, &option_index)) != -1)
    {
        switch (c)
        {
        case 'v':
            verbose = true;
            break;

        case 'n':
            verbose = false;
            break;

        case 'q':
            freopen("/dev/null", "w", stdout);
            break;

        case 'h':
            {
                printf("Options:\n");

                struct option* o = long_options;

                while (o->name)
                {
                    printf("-%c, --%s\n", o->val, o->name);
                    ++o;
                }
                exit(0);
            }
            break;

        case 's':
            printf("Maxscale won't be started\n");
            maxscale::start = false;
            maxscale::manual_debug = true;
            break;

        case 'i':
            printf("Maxscale won't be started and Maxscale.cnf won't be uploaded\n");
            maxscale_init = false;
            break;

        case 'r':
            printf("Nodes are not checked before test and are not restarted\n");
            maxscale::check_nodes = false;
            break;

        case 'g':
            printf("Restarting Galera setup\n");
            maxscale::restart_galera = true;
            break;

        case 'z':
            m_enable_timeouts = false;
            break;

        case 'y':
            printf("Do not use Galera setup\n");
            no_galera = true;
            break;

        case 'l':
            {
                const char* local_ip = optarg ? optarg : "127.0.0.1";
                printf(
                    "MaxScale assumed to be running locally; not started and logs not downloaded. IP: %s\n",
                    local_ip);

                maxscale_init = false;
                m_no_maxscale_log_copy = true;
                m_local_maxscale = true;

                setenv("maxscale_IP", local_ip, true);
                setenv("maxscale_network", local_ip, true);
                setenv("maxscale_private_ip", local_ip, true);
            }
            break;

        case 'm':
            printf("Maxscale will be reinstalled");
            m_reinstall_maxscale = true;
            break;

        default:
            printf("UNKNOWN OPTION: %c\n", c);
            break;
        }
    }

    m_test_name = (optind < argc) ? argv[optind] : basename(argv[0]);
    set_template_and_labels();
    tprintf("Test: '%s', config template: '%s', labels: '%s'",
            m_test_name.c_str(), m_cnf_template_path.c_str(), m_test_labels_str.c_str());
    set_mdbci_labels();

    StringSet missing_mdbci_labels;
    std::set_difference(m_required_mdbci_labels.begin(), m_required_mdbci_labels.end(),
                        m_configured_mdbci_labels.begin(), m_configured_mdbci_labels.end(),
                        std::inserter(missing_mdbci_labels, missing_mdbci_labels.begin()));

    bool mdbci_call_needed = false;
    if (missing_mdbci_labels.empty())
    {
        if (verbose)
        {
            tprintf("Machines with all required labels '%s' are running, MDBCI UP call is not needed",
                    m_mdbci_labels_str.c_str());
        }
    }
    else
    {
        string missing_labels_str = flatten_stringset(missing_mdbci_labels);
        tprintf("Machines with labels '%s' are not running, MDBCI UP call is needed",
                missing_labels_str.c_str());
        mdbci_call_needed = true;
    }

    if (mdbci_call_needed)
    {
        if (call_mdbci(""))
        {
            exit(MDBCI_FAIL);
        }
    }

    if (m_required_mdbci_labels.count(label_repl_be) == 0)
    {
        no_repl = true;
        if (verbose)
        {
            tprintf("No need to use Master/Slave");
        }
    }

    if (m_required_mdbci_labels.count(label_galera_be) == 0)
    {
        no_galera = true;
        if (verbose)
        {
            tprintf("No need to use Galera");
        }
    }

    m_get_logs_command = (string)test_dir + "/get_logs.sh";
    m_ssl_options = string_printf("--ssl-cert=%s/ssl-cert/client-cert.pem --ssl-key=%s/ssl-cert/client-key.pem",
                                  test_dir, test_dir);
    setenv("ssl_options", m_ssl_options.c_str(), 1);

    if (maxscale::require_columnstore)
    {
        cout << "ColumnStore testing is not yet implemented, skipping test" << endl;
        exit(0);
    }

    std::future<bool> repl_future;
    std::future<bool> galera_future;

    if (!no_repl)
    {
        repl = new Mariadb_nodes("node", test_dir, verbose, m_network_config);
        repl->use_ipv6 = m_use_ipv6;
        repl->take_snapshot_command = m_take_snapshot_command.c_str();
        repl->revert_snapshot_command = m_revert_snapshot_command.c_str();
        repl_future = std::async(std::launch::async, &Mariadb_nodes::check_nodes, repl);
    }
    else
    {
        repl = NULL;
    }

    if (!no_galera)
    {
        galera = new Galera_nodes("galera", test_dir, verbose, m_network_config);
        // galera->use_ipv6 = use_ipv6;
        galera->use_ipv6 = false;
        galera->take_snapshot_command = m_take_snapshot_command.c_str();
        galera->revert_snapshot_command = m_revert_snapshot_command.c_str();
        galera_future = std::async(std::launch::async, &Galera_nodes::check_nodes, galera);
    }
    else
    {
        galera = NULL;
    }

    maxscales = new Maxscales("maxscale", test_dir, verbose, m_network_config);

    bool maxscale_ok = maxscales->check_nodes();
    bool repl_ok = no_repl || repl_future.get();
    bool galera_ok = no_galera || galera_future.get();
    bool node_error = !maxscale_ok || !repl_ok || !galera_ok;

    if (node_error || too_many_maxscales())
    {
        tprintf("Recreating VMs: %s", node_error ? "node check failed" : "too many maxscales");

        if (call_mdbci("--recreate"))
        {
            exit(MDBCI_FAIL);
        }
    }

    if (m_reinstall_maxscale && reinstall_maxscales())
    {
        tprintf("Failed to install Maxscale: target is %s", m_target.c_str());
        exit(MDBCI_FAIL);
    }

    std::string src = std::string(test_dir) + "/mdbci/add_core_cnf.sh";
    maxscales->copy_to_node(0, src.c_str(), maxscales->access_homedir[0]);
    maxscales->ssh_node_f(0, true, "%s/add_core_cnf.sh %s", maxscales->access_homedir[0],
                          verbose ? "verbose" : "");


    maxscales->use_ipv6 = m_use_ipv6;
    maxscales->ssl = ssl;

    // Stop MaxScale to prevent it from interfering with the replication setup process
    if (!maxscale::manual_debug)
    {
        for (int i = 0; i < maxscales->N; i++)
        {
            maxscales->stop(i);
        }
    }

    if ((maxscale::restart_galera) && (galera))
    {
        galera->stop_nodes();
        galera->start_replication();
    }

    if (maxscale::check_nodes)
    {
        if (repl && !repl->fix_replication())
        {
            exit(BROKEN_VM_FAIL);
        }
        if (galera && !galera->fix_replication())
        {
            exit(BROKEN_VM_FAIL);
        }
    }

    if (repl && maxscale::required_repl_version.length())
    {
        int ver_repl_required = get_int_version(maxscale::required_repl_version);
        std::string ver_repl = repl->get_lowest_version();
        int int_ver_repl = get_int_version(ver_repl);

        if (int_ver_repl < ver_repl_required)
        {
            tprintf("Test requires a higher version of backend servers, skipping test.");
            tprintf("Required version: %s", maxscale::required_repl_version.c_str());
            tprintf("Master-slave version: %s", ver_repl.c_str());
            exit(0);
        }
    }

    if (galera && maxscale::required_galera_version.length())
    {
        int ver_galera_required = get_int_version(maxscale::required_galera_version);
        std::string ver_galera = galera->get_lowest_version();
        int int_ver_galera = get_int_version(ver_galera);

        if (int_ver_galera < ver_galera_required)
        {
            tprintf("Test requires a higher version of backend servers, skipping test.");
            tprintf("Required version: %s", maxscale::required_galera_version.c_str());
            tprintf("Galera version: %s", ver_galera.c_str());
            exit(0);
        }
    }

    if (maxscale_init)
    {
        init_maxscales();
    }

    if (backend_ssl)
    {
        tprintf("Configuring backends for ssl \n");
        repl->configure_ssl(true);
        if (galera)
        {
            galera->configure_ssl(false);
            galera->start_replication();
        }
    }

    if (mdbci_call_needed)
    {
        int ec;
        char* ver = maxscales->ssh_node_output(0, "maxscale --version-full", false, &ec);
        if (ec)
        {
            tprintf("Error retrival of Maxscale version info");
        }
        else
        {
            tprintf("Maxscale_full_version_start:\n%s\nMaxscale_full_version_end\n", ver);
        }
    }

    char str[1024];
    sprintf(str, "mkdir -p LOGS/%s", m_test_name.c_str());
    system(str);

    timeout = 999999999;
    set_log_copy_interval(999999999);
    pthread_create(&timeout_thread_p, NULL, timeout_thread, this);
    pthread_create(&log_copy_thread_p, NULL, log_copy_thread, this);
    tprintf("Starting test");
    gettimeofday(&m_start_time, NULL);
}

TestConnections::~TestConnections()
{
    for (auto& a : m_on_destroy)
    {
        a();
    }

    if (backend_ssl)
    {
        repl->disable_ssl();
        // galera->disable_ssl();
    }

    // stop all Maxscales to detect crashes on exit
    for (int i = 0; i < maxscales->N; i++)
    {
        stop_maxscale(i);
    }

    if (maxscales->use_valgrind)
    {
        sleep(15);      // sleep to let logs be written do disks
    }

    copy_all_logs();

    /* Temporary disable snapshot revert due to Galera failures
     *  if (global_result != 0 )
     *  {
     *   if (no_vm_revert)
     *   {
     *       tprintf("no_vm_revert flag is set, not reverting VMs\n");
     *   }
     *   else
     *   {
     *       tprintf("Reverting snapshot\n");
     *       revert_snapshot((char*) "clean");
     *   }
     *  }
     */

    if (repl)
    {
        delete repl;
    }
    if (galera)
    {
        delete galera;
    }

    if (maxscale::multiple_maxscales)
    {
        maxscales->stop_all();
    }

    if (global_result)
    {
        // This causes the test to fail if a core dump is found
        exit(1);
    }
}

void TestConnections::report_result(const char* format, va_list argp)
{
    timeval t2;
    gettimeofday(&t2, NULL);
    double elapsedTime = (t2.tv_sec - m_start_time.tv_sec);
    elapsedTime += (double) (t2.tv_usec - m_start_time.tv_usec) / 1000000.0;

    global_result += 1;

    printf("%04f: TEST_FAILED! ", elapsedTime);

    vprintf(format, argp);

    if (format[strlen(format) - 1] != '\n')
    {
        printf("\n");
    }
}

void TestConnections::add_result(bool result, const char* format, ...)
{
    if (result)
    {
        va_list argp;
        va_start(argp, format);
        report_result(format, argp);
        va_end(argp);
    }
}

void TestConnections::expect(bool result, const char* format, ...)
{
    if (!result)
    {
        va_list argp;
        va_start(argp, format);
        report_result(format, argp);
        va_end(argp);
    }
}

void TestConnections::read_mdbci_info()
{
    m_mdbci_vm_path = envvar_get_set("MDBCI_VM_PATH", "%s/vms/", getenv("HOME"));

    string cmd = "mkdir -p " + m_mdbci_vm_path;
    if (system(cmd.c_str()))
    {
        tprintf("Unable to create MDBCI VMs direcory '%s', exiting", m_mdbci_vm_path.c_str());
        exit(MDBCI_FAIL);
    }
    m_mdbci_template = envvar_get_set("template", "default");
    m_target = envvar_get_set("target", "develop");

    m_mdbci_config_name = envvar_get_set("mdbci_config_name", "local");
    m_vm_path = m_mdbci_vm_path + "/" + m_mdbci_config_name;

    if (!m_mdbci_config_name.empty())
    {
        std::ifstream nc_file;
        nc_file.open(m_vm_path + "_network_config");
        std::stringstream strStream;
        strStream << nc_file.rdbuf();
        m_network_config = strStream.str();
        nc_file.close();

        nc_file.open(m_vm_path + "_configured_labels");
        std::stringstream strStream1;
        strStream1 << nc_file.rdbuf();
        m_configured_mdbci_labels = parse_to_stringset(strStream1.str());
        nc_file.close();
    }
    else
    {
        tprintf("The name of MDBCI configuration is not defined, exiting!");
        exit(1);
    }
    if (verbose)
    {
        tprintf(m_network_config.c_str());
    }
}

void TestConnections::read_env()
{
    read_mdbci_info();
    if (verbose)
    {
        printf("Reading test setup configuration from environmental variables\n");
    }

    ssl = readenv_bool("ssl", true);

    if (readenv_bool("mysql51_only", false) || readenv_bool("no_nodes_check", false))
    {
        maxscale::check_nodes = false;
    }

    if (readenv_bool("no_maxscale_start", false))
    {
        maxscale::start = false;
    }

    m_no_backend_log_copy = readenv_bool("no_backend_log_copy", false);
    m_no_maxscale_log_copy = readenv_bool("no_maxscale_log_copy", false);
    m_use_ipv6 = readenv_bool("use_ipv6", false);
    backend_ssl = readenv_bool("backend_ssl", false);
    smoke = readenv_bool("smoke", false);
    m_threads = readenv_int("threads", 4);
    m_use_snapshots = readenv_bool("use_snapshots", false);
    m_take_snapshot_command = envvar_get_set(
        "take_snapshot_command", "mdbci snapshot take --path-to-nodes %s --snapshot-name ",
        m_mdbci_config_name.c_str());
    m_revert_snapshot_command = envvar_get_set(
        "revert_snapshot_command", "mdbci snapshot revert --path-to-nodes %s --snapshot-name ",
        m_mdbci_config_name.c_str());
    no_vm_revert = readenv_bool("no_vm_revert", true);
}

void TestConnections::print_env()
{
    printf("Maxscale IP\t%s\n", maxscales->IP[0]);
    printf("Maxscale User name\t%s\n", maxscales->user_name);
    printf("Maxscale Password\t%s\n", maxscales->password);
    printf("Maxscale SSH key\t%s\n", maxscales->sshkey[0]);
    printf("Maxadmin password\t%s\n", maxscales->maxadmin_password[0]);
    printf("Access user\t%s\n", maxscales->access_user[0]);
    if (repl)
    {
        repl->print_env();
    }
    if (galera)
    {
        galera->print_env();
    }
}

/**
 * Set config template file and test labels.
 */
void TestConnections::set_template_and_labels()
{
    const TestDefinition* found = nullptr;
    for (int i = 0; test_definitions[i].name; i++)
    {
        auto* test = &test_definitions[i];
        if (test->name == m_test_name)
        {
            found = test;
            break;
        }
    }

    if (found)
    {
        m_cnf_template_path = found->config_template;
        m_test_labels_str = found->labels;
    }
    else
    {
        printf("Failed to find configuration template for test '%s', using default template '%s' and "
               "labels '%s'.\n",
               m_test_name.c_str(), default_template, label_repl_be.c_str());
        m_cnf_template_path = default_template;
        m_test_labels_str = label_repl_be;
    }

    // Parse the labels-string to a set.
    m_test_labels = parse_to_stringset(m_test_labels_str);
}

void TestConnections::process_template(int m, const string& cnf_template_path, const char* dest)
{
    struct stat stb;
    char str[4096];
    string template_file = cnf_template_path;

    char extended_template_file[1024];
    sprintf(extended_template_file, "%s.%03d", cnf_template_path.c_str(), m);
    if (stat(extended_template_file, &stb) == 0)
    {
        template_file = extended_template_file;
    }

    tprintf("Template file is %s\n", template_file.c_str());

    sprintf(str, "cp %s maxscale.cnf", template_file.c_str());
    if (verbose)
    {
        tprintf("Executing '%s' command\n", str);
    }
    if (system(str) != 0)
    {
        tprintf("Error copying maxscale.cnf template\n");
        return;
    }

    if (backend_ssl)
    {
        tprintf("Adding ssl settings\n");
        const char sed_cmd[] = "sed -i "
                               "\"s|type=server|type=server\\nssl=required\\nssl_cert=/###access_homedir###/"
                               "certs/client-cert.pem\\nssl_key=/###access_homedir###/certs/client-key.pem"
                               "\\nssl_ca_cert=/###access_homedir###/certs/ca.pem|g\" maxscale.cnf";
        system(sed_cmd);
    }

    sprintf(str, "sed -i \"s/###threads###/%d/\"  maxscale.cnf", m_threads);
    system(str);

    Mariadb_nodes* mdn[2];
    char* IPcnf;
    mdn[0] = repl;
    mdn[1] = galera;
    int i, j;
    int mdn_n = galera ? 2 : 1;

    for (j = 0; j < mdn_n; j++)
    {
        if (mdn[j])
        {
            for (i = 0; i < mdn[j]->N; i++)
            {
                if (mdn[j]->use_ipv6)
                {
                    IPcnf = mdn[j]->IP6[i];
                }
                else
                {
                    IPcnf = mdn[j]->IP_private[i];
                }
                sprintf(str, "sed -i \"s/###%s_server_IP_%0d###/%s/\" maxscale.cnf",
                        mdn[j]->prefix, i + 1, IPcnf);
                system(str);

                sprintf(str, "sed -i \"s/###%s_server_port_%0d###/%d/\" maxscale.cnf",
                        mdn[j]->prefix, i + 1, mdn[j]->port[i]);
                system(str);
            }

            sprintf(str,
                    "sed -i \"s/###%s###/%s/\" maxscale.cnf",
                    mdn[j]->cnf_server_name.c_str(), mdn[j]->cnf_servers().c_str());
            system(str);
            sprintf(str,
                    "sed -i \"s/###%s_line###/%s/\" maxscale.cnf",
                    mdn[j]->cnf_server_name.c_str(), mdn[j]->cnf_servers_line().c_str());
            system(str);

            mdn[j]->connect();
            execute_query(mdn[j]->nodes[0], (char*) "CREATE DATABASE IF NOT EXISTS test");
            mdn[j]->close_connections();
        }
    }

    sprintf(str, "sed -i \"s/###access_user###/%s/g\" maxscale.cnf", maxscales->access_user[m]);
    system(str);

    sprintf(str, "sed -i \"s|###access_homedir###|%s|g\" maxscale.cnf", maxscales->access_homedir[m]);
    system(str);

    if (repl && repl->v51)
    {
        system("sed -i \"s/###repl51###/mysql51_replication=true/g\" maxscale.cnf");
    }
    maxscales->copy_to_node_legacy((char*) "maxscale.cnf", (char*) dest, m);
}

void TestConnections::init_maxscales()
{
    // Always initialize the first MaxScale
    init_maxscale(0);

    if (maxscale::multiple_maxscales)
    {
        for (int i = 1; i < maxscales->N; i++)
        {
            init_maxscale(i);
        }
    }
}

void TestConnections::init_maxscale(int m)
{
    process_template(m, m_cnf_template_path, maxscales->access_homedir[m]);
    if (maxscales->ssh_node_f(m, true, "test -d %s/certs", maxscales->access_homedir[m]))
    {
        tprintf("SSL certificates not found, copying to maxscale");
        maxscales->ssh_node_f(m,
                              true,
                              "rm -rf %s/certs;mkdir -m a+wrx %s/certs;",
                              maxscales->access_homedir[m],
                              maxscales->access_homedir[m]);

        char str[4096];
        char dtr[4096];
        sprintf(str, "%s/ssl-cert/*", test_dir);
        sprintf(dtr, "%s/certs/", maxscales->access_homedir[m]);
        maxscales->copy_to_node_legacy(str, dtr, m);
        sprintf(str, "cp %s/ssl-cert/* .", test_dir);
        system(str);
        maxscales->ssh_node_f(m, true, "chmod -R a+rx %s;", maxscales->access_homedir[m]);
    }

    maxscales->ssh_node_f(m,
                          true,
                          "cp maxscale.cnf %s;"
                          "iptables -F INPUT;"
                          "rm -rf %s/*.log /tmp/core* /dev/shm/* /var/lib/maxscale/maxscale.cnf.d/ /var/lib/maxscale/*;",
                          maxscales->maxscale_cnf[m],
                          maxscales->maxscale_log_dir[m]);
    if (maxscale::start)
    {
        maxscales->restart_maxscale(m);
        maxscales->ssh_node_f(m,
                              true,
                              "maxctrl api get maxscale/debug/monitor_wait");
    }
}

void TestConnections::copy_one_mariadb_log(Mariadb_nodes* nrepl, int i, std::string filename)
{
    auto log_retrive_commands =
    {
        "cat /var/lib/mysql/*.err",
        "cat /var/log/syslog | grep mysql",
        "cat /var/log/messages | grep mysql"
    };

    int j = 1;

    for (auto cmd : log_retrive_commands)
    {
        auto output = nrepl->ssh_output(cmd, i).second;

        if (!output.empty())
        {
            std::ofstream outfile(filename + std::to_string(j++));

            if (outfile)
            {
                outfile << output;
            }
        }
    }
}

int TestConnections::copy_mariadb_logs(Mariadb_nodes* nrepl,
                                       const char* prefix,
                                       std::vector<std::thread>& threads)
{
    int local_result = 0;

    if (nrepl)
    {
        for (int i = 0; i < nrepl->N; i++)
        {
            // Do not copy MariaDB logs in case of local backend
            if (strcmp(nrepl->IP[i], "127.0.0.1") != 0)
            {
                char str[4096];
                sprintf(str, "LOGS/%s/%s%d_mariadb_log", m_test_name.c_str(), prefix, i);
                threads.emplace_back(&TestConnections::copy_one_mariadb_log, this, nrepl, i, str);
            }
        }
    }

    return local_result;
}

int TestConnections::copy_all_logs()
{
    set_timeout(300);

    char str[PATH_MAX + 1];
    sprintf(str, "mkdir -p LOGS/%s", m_test_name.c_str());
    system(str);

    std::vector<std::thread> threads;

    if (!m_no_backend_log_copy)
    {
        copy_mariadb_logs(repl, "node", threads);
        copy_mariadb_logs(galera, "galera", threads);
    }

    int rv = 0;

    if (!m_no_maxscale_log_copy)
    {
        rv = copy_maxscale_logs(0);
    }

    for (auto& a : threads)
    {
        a.join();
    }

    return rv;
}
int TestConnections::copy_maxscale_logs(double timestamp)
{
    char log_dir[1024];
    char log_dir_i[1024];
    char sys[1024];
    if (timestamp == 0)
    {
        sprintf(log_dir, "LOGS/%s", m_test_name.c_str());
    }
    else
    {
        sprintf(log_dir, "LOGS/%s/%04f", m_test_name.c_str(), timestamp);
    }
    for (int i = 0; i < maxscales->N; i++)
    {
        sprintf(log_dir_i, "%s/%03d", log_dir, i);
        sprintf(sys, "mkdir -p %s", log_dir_i);
        system(sys);
        if (strcmp(maxscales->IP[i], "127.0.0.1") != 0)
        {
            int rc = maxscales->ssh_node_f(i, true,
                                           "rm -rf %s/logs;"
                                           "mkdir %s/logs;"
                                           "cp %s/*.log %s/logs/;"
                                           "cp /tmp/core* %s/logs/;"
                                           "cp %s %s/logs/;"
                                           "chmod 777 -R %s/logs;"
                                           "ls /tmp/core* && exit 42;",
                                           maxscales->access_homedir[i],
                                           maxscales->access_homedir[i],
                                           maxscales->maxscale_log_dir[i],
                                           maxscales->access_homedir[i],
                                           maxscales->access_homedir[i],
                                           maxscales->maxscale_cnf[i],
                                           maxscales->access_homedir[i],
                                           maxscales->access_homedir[i]);
            sprintf(sys, "%s/logs/*", maxscales->access_homedir[i]);
            maxscales->copy_from_node(i, sys, log_dir_i);
            expect(rc != 42, "Test should not generate core files");
        }
        else
        {
            maxscales->ssh_node_f(i, true, "cp %s/*.logs %s/", maxscales->maxscale_log_dir[i], log_dir_i);
            maxscales->ssh_node_f(i, true, "cp /tmp/core* %s/", log_dir_i);
            maxscales->ssh_node_f(i, true, "cp %s %s/", maxscales->maxscale_cnf[i], log_dir_i);
            maxscales->ssh_node_f(i, true, "chmod a+r -R %s", log_dir_i);
        }
    }
    return 0;
}

int TestConnections::copy_all_logs_periodic()
{
    timeval t2;
    gettimeofday(&t2, NULL);
    double elapsedTime = (t2.tv_sec - m_start_time.tv_sec);
    elapsedTime += (double) (t2.tv_usec - m_start_time.tv_usec) / 1000000.0;

    return copy_maxscale_logs(elapsedTime);
}

int TestConnections::prepare_binlog(int m)
{
    char version_str[1024] = "";
    repl->connect();
    find_field(repl->nodes[0], "SELECT @@version", "@@version", version_str);
    tprintf("Master server version '%s'", version_str);

    if (*version_str
        && strstr(version_str, "10.0") == NULL
        && strstr(version_str, "10.1") == NULL
        && strstr(version_str, "10.2") == NULL)
    {
        add_result(maxscales->ssh_node_f(m,
                                         true,
                                         "sed -i \"s/,mariadb10-compatibility=1//\" %s",
                                         maxscales->maxscale_cnf[m]),
                   "Error editing maxscale.cnf");
    }

    if (!m_local_maxscale)
    {
        tprintf("Removing all binlog data from Maxscale node");
        add_result(maxscales->ssh_node_f(m, true, "rm -rf %s", maxscales->maxscale_binlog_dir[m]),
                   "Removing binlog data failed");

        tprintf("Creating binlog dir");
        add_result(maxscales->ssh_node_f(m, true, "mkdir -p %s", maxscales->maxscale_binlog_dir[m]),
                   "Creating binlog data dir failed");
        tprintf("Set 'maxscale' as a owner of binlog dir");
        add_result(maxscales->ssh_node_f(m,
                                         false,
                                         "%s mkdir -p %s; %s chown maxscale:maxscale -R %s",
                                         maxscales->access_sudo[m],
                                         maxscales->maxscale_binlog_dir[m],
                                         maxscales->access_sudo[m],
                                         maxscales->maxscale_binlog_dir[m]),
                   "directory ownership change failed");
    }
    else
    {
        perform_manual_action("Remove all local binlog data");
    }

    return 0;
}

int TestConnections::start_binlog(int m)
{
    char sys1[4096];
    MYSQL* binlog;
    char log_file[256];
    char log_pos[256];
    char cmd_opt[256];

    int i;
    int global_result = 0;
    bool no_pos;

    no_pos = repl->no_set_pos;

    switch (binlog_cmd_option)
    {
    case 1:
        sprintf(cmd_opt, "--binlog-checksum=CRC32");
        break;

    case 2:
        sprintf(cmd_opt, "--binlog-checksum=NONE");
        break;

    default:
        sprintf(cmd_opt, " ");
    }

    repl->stop_nodes();

    if (!m_local_maxscale)
    {
        binlog =
            open_conn_no_db(maxscales->binlog_port[m], maxscales->IP[m], repl->user_name, repl->password,
                            ssl);
        execute_query(binlog, "stop slave");
        execute_query(binlog, "reset slave all");
        mysql_close(binlog);

        tprintf("Stopping maxscale\n");
        add_result(maxscales->stop_maxscale(m), "Maxscale stopping failed\n");
    }
    else
    {
        perform_manual_action(
            "Perform the equivalent of 'STOP SLAVE; RESET SLAVE ALL' and stop local Maxscale");
    }

    for (i = 0; i < repl->N; i++)
    {
        repl->start_node(i, cmd_opt);
    }
    sleep(5);

    tprintf("Connecting to all backend nodes\n");
    repl->connect();

    tprintf("Stopping everything\n");
    for (i = 0; i < repl->N; i++)
    {
        execute_query(repl->nodes[i], "stop slave");
        execute_query(repl->nodes[i], "reset slave all");
        execute_query(repl->nodes[i], "reset master");
    }
    prepare_binlog(m);
    tprintf("Testing binlog when MariaDB is started with '%s' option\n", cmd_opt);

    if (!m_local_maxscale)
    {
        tprintf("ls binlog data dir on Maxscale node\n");
        add_result(maxscales->ssh_node_f(m, true, "ls -la %s/", maxscales->maxscale_binlog_dir[m]),
                   "ls failed\n");
    }

    if (binlog_master_gtid)
    {
        // GTID to connect real Master
        tprintf("GTID for connection 1st slave to master!\n");
        try_query(repl->nodes[1], "stop slave");
        try_query(repl->nodes[1], "SET @@global.gtid_slave_pos=''");
        sprintf(sys1,
                "CHANGE MASTER TO MASTER_HOST='%s', MASTER_PORT=%d, MASTER_USER='repl', MASTER_PASSWORD='repl', MASTER_USE_GTID=Slave_pos",
                repl->IP_private[0],
                repl->port[0]);
        try_query(repl->nodes[1], "%s", sys1);
        try_query(repl->nodes[1], "start slave");
    }
    else
    {
        tprintf("show master status\n");
        find_field(repl->nodes[0], (char*) "show master status", (char*) "File", &log_file[0]);
        find_field(repl->nodes[0], (char*) "show master status", (char*) "Position", &log_pos[0]);
        tprintf("Real master file: %s\n", log_file);
        tprintf("Real master pos : %s\n", log_pos);

        tprintf("Stopping first slave (node 1)\n");
        try_query(repl->nodes[1], "stop slave;");
        // repl->no_set_pos = true;
        repl->no_set_pos = false;
        tprintf("Configure first backend slave node to be slave of real master\n");
        repl->set_slave(repl->nodes[1], repl->IP_private[0], repl->port[0], log_file, log_pos);
    }

    if (!m_local_maxscale)
    {
        tprintf("Starting back Maxscale\n");
        add_result(maxscales->start_maxscale(m), "Maxscale start failed\n");
    }
    else
    {
        perform_manual_action("Start Maxscale");
    }

    tprintf("Connecting to MaxScale binlog router (with any DB)\n");
    binlog =
        open_conn_no_db(maxscales->binlog_port[m], maxscales->IP[m], repl->user_name, repl->password, ssl);

    add_result(mysql_errno(binlog), "Error connection to binlog router %s\n", mysql_error(binlog));

    if (binlog_master_gtid)
    {
        // GTID to connect real Master
        tprintf("GTID for connection binlog router to master!\n");
        try_query(binlog, "stop slave");
        try_query(binlog, "SET @@global.gtid_slave_pos=''");
        sprintf(sys1,
                "CHANGE MASTER TO MASTER_HOST='%s', MASTER_PORT=%d, MASTER_USER='repl', MASTER_PASSWORD='repl', MASTER_USE_GTID=Slave_pos",
                repl->IP_private[0],
                repl->port[0]);
        try_query(binlog, "%s", sys1);
    }
    else
    {
        repl->no_set_pos = true;
        tprintf("configuring Maxscale binlog router\n");
        repl->set_slave(binlog, repl->IP_private[0], repl->port[0], log_file, log_pos);
    }
    // ssl between binlog router and Master
    if (backend_ssl)
    {
        sprintf(sys1,
                "CHANGE MASTER TO master_ssl_cert='%s/certs/client-cert.pem', master_ssl_ca='%s/certs/ca.pem', master_ssl=1, master_ssl_key='%s/certs/client-key.pem'",
                maxscales->access_homedir[m],
                maxscales->access_homedir[m],
                maxscales->access_homedir[m]);
        tprintf("Configuring Master ssl: %s\n", sys1);
        try_query(binlog, "%s", sys1);
    }
    try_query(binlog, "start slave");
    try_query(binlog, "show slave status");

    if (binlog_slave_gtid)
    {
        tprintf("GTID for connection slaves to binlog router!\n");
        tprintf("Setup all backend nodes except first one to be slaves of binlog Maxscale node\n");
        fflush(stdout);
        for (i = 2; i < repl->N; i++)
        {
            try_query(repl->nodes[i], "stop slave");
            try_query(repl->nodes[i], "SET @@global.gtid_slave_pos=''");
            sprintf(sys1,
                    "CHANGE MASTER TO MASTER_HOST='%s', MASTER_PORT=%d, MASTER_USER='repl', MASTER_PASSWORD='repl', MASTER_USE_GTID=Slave_pos",
                    maxscales->IP_private[m],
                    maxscales->binlog_port[m]);
            try_query(repl->nodes[i], "%s", sys1);
            try_query(repl->nodes[i], "start slave");
        }
    }
    else
    {
        repl->no_set_pos = false;

        // get Master status from Maxscale binlog
        tprintf("show master status\n");
        find_field(binlog, (char*) "show master status", (char*) "File", &log_file[0]);
        find_field(binlog, (char*) "show master status", (char*) "Position", &log_pos[0]);

        tprintf("Maxscale binlog master file: %s\n", log_file);
        tprintf("Maxscale binlog master pos : %s\n", log_pos);

        tprintf("Setup all backend nodes except first one to be slaves of binlog Maxscale node\n");
        fflush(stdout);
        for (i = 2; i < repl->N; i++)
        {
            try_query(repl->nodes[i], "stop slave");
            repl->set_slave(repl->nodes[i], maxscales->IP_private[m], maxscales->binlog_port[m],
                            log_file, log_pos);
        }
    }

    repl->close_connections();
    try_query(binlog, "show slave status");
    mysql_close(binlog);
    repl->no_set_pos = no_pos;
    return global_result;
}

bool TestConnections::replicate_from_master(int m)
{
    bool rval = true;

    /** Stop the binlogrouter */
    MYSQL* conn = open_conn_no_db(maxscales->binlog_port[m],
                                  maxscales->IP[m],
                                  repl->user_name,
                                  repl->password,
                                  ssl);
    execute_query_silent(conn, "stop slave");
    mysql_close(conn);

    repl->execute_query_all_nodes("STOP SLAVE");

    /** Clean up MaxScale directories */
    maxscales->stop_maxscale(m);
    prepare_binlog(m);
    maxscales->start_maxscale(m);

    char log_file[256] = "";
    char log_pos[256] = "4";

    repl->connect();
    execute_query(repl->nodes[0], "RESET MASTER");

    conn = open_conn_no_db(maxscales->binlog_port[m], maxscales->IP[m], repl->user_name, repl->password, ssl);

    if (find_field(repl->nodes[0], "show master status", "File", log_file)
        || repl->set_slave(conn, repl->IP_private[0], repl->port[0], log_file, log_pos)
        || execute_query(conn, "start slave"))
    {
        rval = false;
    }

    mysql_close(conn);

    return rval;
}

void TestConnections::revert_replicate_from_master()
{
    char log_file[256] = "";

    repl->connect();
    execute_query(repl->nodes[0], "RESET MASTER");
    find_field(repl->nodes[0], "show master status", "File", log_file);

    for (int i = 1; i < repl->N; i++)
    {
        repl->set_slave(repl->nodes[i], repl->IP_private[0], repl->port[0], log_file, (char*)"4");
        execute_query(repl->nodes[i], "start slave");
    }
}

int TestConnections::start_mm(int m)
{
    int i;
    char log_file1[256];
    char log_pos1[256];
    char log_file2[256];
    char log_pos2[256];

    tprintf("Stopping maxscale\n");
    int global_result = maxscales->stop_maxscale(m);

    tprintf("Stopping all backend nodes\n");
    global_result += repl->stop_nodes();

    for (i = 0; i < 2; i++)
    {
        tprintf("Starting back node %d\n", i);
        global_result += repl->start_node(i, (char*) "");
    }

    repl->connect();
    for (i = 0; i < 2; i++)
    {
        execute_query(repl->nodes[i], "stop slave");
        execute_query(repl->nodes[i], "reset master");
    }

    execute_query(repl->nodes[0], "SET GLOBAL READ_ONLY=ON");

    find_field(repl->nodes[0], (char*) "show master status", (char*) "File", log_file1);
    find_field(repl->nodes[0], (char*) "show master status", (char*) "Position", log_pos1);

    find_field(repl->nodes[1], (char*) "show master status", (char*) "File", log_file2);
    find_field(repl->nodes[1], (char*) "show master status", (char*) "Position", log_pos2);

    repl->set_slave(repl->nodes[0], repl->IP_private[1], repl->port[1], log_file2, log_pos2);
    repl->set_slave(repl->nodes[1], repl->IP_private[0], repl->port[0], log_file1, log_pos1);

    repl->close_connections();

    tprintf("Starting back Maxscale\n");
    global_result += maxscales->start_maxscale(m);

    return global_result;
}

bool TestConnections::log_matches(int m, const char* pattern)
{

    // Replace single quotes with wildcard characters, should solve most problems
    std::string p = pattern;
    for (auto& a : p)
    {
        if (a == '\'')
        {
            a = '.';
        }
    }

    return maxscales->ssh_node_f(m, true, "grep '%s' /var/log/maxscale/maxscale*.log", p.c_str()) == 0;
}

void TestConnections::log_includes(int m, const char* pattern)
{
    add_result(!log_matches(m, pattern), "Log does not match pattern '%s'", pattern);
}

void TestConnections::log_excludes(int m, const char* pattern)
{
    add_result(log_matches(m, pattern), "Log matches pattern '%s'", pattern);
}

static int read_log(const char* name, char** err_log_content_p)
{
    FILE* f;
    *err_log_content_p = NULL;
    char* err_log_content;
    f = fopen(name, "rb");
    if (f != NULL)
    {

        int prev = ftell(f);
        fseek(f, 0L, SEEK_END);
        long int size = ftell(f);
        fseek(f, prev, SEEK_SET);
        err_log_content = (char*)malloc(size + 2);
        if (err_log_content != NULL)
        {
            fread(err_log_content, 1, size, f);
            for (int i = 0; i < size; i++)
            {
                if (err_log_content[i] == 0)
                {
                    // printf("null detected at position %d\n", i);
                    err_log_content[i] = '\n';
                }
            }
            // printf("s=%ld\n", strlen(err_log_content));
            err_log_content[size] = '\0';
            // printf("s=%ld\n", strlen(err_log_content));
            * err_log_content_p = err_log_content;
            fclose(f);
            return 0;
        }
        else
        {
            printf("Error allocationg memory for the log\n");
            return 1;
        }
    }
    else
    {
        printf ("Error reading log %s \n", name);
        return 1;
    }
}

int TestConnections::find_connected_slave(int m, int* global_result)
{
    int conn_num;
    int all_conn = 0;
    int current_slave = -1;
    repl->connect();
    for (int i = 0; i < repl->N; i++)
    {
        conn_num = get_conn_num(repl->nodes[i], maxscales->ip(m), maxscales->hostname[m], (char*) "test");
        tprintf("connections to %d: %u\n", i, conn_num);
        if ((i == 0) && (conn_num != 1))
        {
            tprintf("There is no connection to master\n");
            *global_result = 1;
        }
        all_conn += conn_num;
        if ((i != 0) && (conn_num != 0))
        {
            current_slave = i;
        }
    }
    if (all_conn != 2)
    {
        tprintf("total number of connections is not 2, it is %d\n", all_conn);
        *global_result = 1;
    }
    tprintf("Now connected slave node is %d (%s)\n", current_slave, repl->IP[current_slave]);
    repl->close_connections();
    return current_slave;
}

int TestConnections::find_connected_slave1(int m)
{
    int conn_num;
    int all_conn = 0;
    int current_slave = -1;
    repl->connect();
    for (int i = 0; i < repl->N; i++)
    {
        conn_num = get_conn_num(repl->nodes[i], maxscales->ip(m), maxscales->hostname[m], (char*) "test");
        tprintf("connections to %d: %u\n", i, conn_num);
        all_conn += conn_num;
        if ((i != 0) && (conn_num != 0))
        {
            current_slave = i;
        }
    }
    tprintf("Now connected slave node is %d (%s)\n", current_slave, repl->IP[current_slave]);
    repl->close_connections();
    return current_slave;
}

int TestConnections::check_maxscale_processes(int m, int expected)
{
    const char* ps_cmd = maxscales->use_valgrind ?
        "ps ax | grep valgrind | grep maxscale | grep -v grep | wc -l" :
        "ps -C maxscale | grep maxscale | wc -l";

    int exit_code;
    char* maxscale_num = maxscales->ssh_node_output(m, ps_cmd, false, &exit_code);

    if ((maxscale_num == NULL) || (exit_code != 0))
    {
        return -1;
    }
    char* nl = strchr(maxscale_num, '\n');
    if (nl)
    {
        *nl = '\0';
    }

    if (atoi(maxscale_num) != expected)
    {
        tprintf("%s maxscale processes detected, trying again in 5 seconds\n", maxscale_num);
        sleep(5);
        maxscale_num = maxscales->ssh_node_output(m, ps_cmd, false, &exit_code);

        if (atoi(maxscale_num) != expected)
        {
            add_result(1, "Number of MaxScale processes is not %d, it is %s\n", expected, maxscale_num);
        }
    }

    return exit_code;
}

int TestConnections::stop_maxscale(int m)
{
    int res = maxscales->stop_maxscale(m);
    check_maxscale_processes(m, 0);
    fflush(stdout);
    return res;
}

int TestConnections::start_maxscale(int m)
{
    int res = maxscales->start_maxscale(m);
    check_maxscale_processes(m, 1);
    fflush(stdout);
    return res;
}

int TestConnections::check_maxscale_alive(int m)
{
    int gr = global_result;
    set_timeout(10);
    tprintf("Connecting to Maxscale\n");
    add_result(maxscales->connect_maxscale(m), "Can not connect to Maxscale\n");
    tprintf("Trying simple query against all sevices\n");
    tprintf("RWSplit \n");
    set_timeout(10);
    try_query(maxscales->conn_rwsplit[m], "show databases;");
    tprintf("ReadConn Master \n");
    set_timeout(10);
    try_query(maxscales->conn_master[m], "show databases;");
    tprintf("ReadConn Slave \n");
    set_timeout(10);
    try_query(maxscales->conn_slave[m], "show databases;");
    set_timeout(10);
    maxscales->close_maxscale_connections(m);
    add_result(global_result - gr, "Maxscale is not alive\n");
    stop_timeout();
    check_maxscale_processes(m, 1);

    return global_result - gr;
}

int TestConnections::test_maxscale_connections(int m, bool rw_split, bool rc_master, bool rc_slave)
{
    int rval = 0;
    int rc;

    tprintf("Testing RWSplit, expecting %s\n", (rw_split ? "success" : "failure"));
    rc = execute_query(maxscales->conn_rwsplit[m], "select 1");
    if ((rc == 0) != rw_split)
    {
        tprintf("Error: Query %s\n", (rw_split ? "failed" : "succeeded"));
        rval++;
    }

    tprintf("Testing ReadConnRoute Master, expecting %s\n", (rc_master ? "success" : "failure"));
    rc = execute_query(maxscales->conn_master[m], "select 1");
    if ((rc == 0) != rc_master)
    {
        tprintf("Error: Query %s", (rc_master ? "failed" : "succeeded"));
        rval++;
    }

    tprintf("Testing ReadConnRoute Slave, expecting %s\n", (rc_slave ? "success" : "failure"));
    rc = execute_query(maxscales->conn_slave[m], "select 1");
    if ((rc == 0) != rc_slave)
    {
        tprintf("Error: Query %s", (rc_slave ? "failed" : "succeeded"));
        rval++;
    }
    return rval;
}


int TestConnections::create_connections(int m,
                                        int conn_N,
                                        bool rwsplit_flag,
                                        bool master_flag,
                                        bool slave_flag,
                                        bool galera_flag)
{
    int i;
    int local_result = 0;
    MYSQL* rwsplit_conn[conn_N];
    MYSQL* master_conn[conn_N];
    MYSQL* slave_conn[conn_N];
    MYSQL* galera_conn[conn_N];


    tprintf("Opening %d connections to each router\n", conn_N);
    for (i = 0; i < conn_N; i++)
    {
        set_timeout(20);

        if (verbose)
        {
            tprintf("opening %d-connection: ", i + 1);
        }

        if (rwsplit_flag)
        {
            if (verbose)
            {
                printf("RWSplit \t");
            }

            rwsplit_conn[i] = maxscales->open_rwsplit_connection(m);
            if (!rwsplit_conn[i])
            {
                local_result++;
                tprintf("RWSplit connection failed\n");
            }
        }
        if (master_flag)
        {
            if (verbose)
            {
                printf("ReadConn master \t");
            }

            master_conn[i] = maxscales->open_readconn_master_connection(m);
            if (mysql_errno(master_conn[i]) != 0)
            {
                local_result++;
                tprintf("ReadConn master connection failed, error: %s\n", mysql_error(master_conn[i]));
            }
        }
        if (slave_flag)
        {
            if (verbose)
            {
                printf("ReadConn slave \t");
            }

            slave_conn[i] = maxscales->open_readconn_slave_connection(m);
            if (mysql_errno(slave_conn[i]) != 0)
            {
                local_result++;
                tprintf("ReadConn slave connection failed, error: %s\n", mysql_error(slave_conn[i]));
            }
        }
        if (galera_flag)
        {
            if (verbose)
            {
                printf("Galera \n");
            }

            galera_conn[i] =
                open_conn(4016, maxscales->IP[m], maxscales->user_name, maxscales->password, ssl);
            if (mysql_errno(galera_conn[i]) != 0)
            {
                local_result++;
                tprintf("Galera connection failed, error: %s\n", mysql_error(galera_conn[i]));
            }
        }
    }
    for (i = 0; i < conn_N; i++)
    {
        set_timeout(20);

        if (verbose)
        {
            tprintf("Trying query against %d-connection: ", i + 1);
        }

        if (rwsplit_flag)
        {
            if (verbose)
            {
                tprintf("RWSplit \t");
            }
            local_result += execute_query(rwsplit_conn[i], "select 1;");
        }
        if (master_flag)
        {
            if (verbose)
            {
                tprintf("ReadConn master \t");
            }
            local_result += execute_query(master_conn[i], "select 1;");
        }
        if (slave_flag)
        {
            if (verbose)
            {
                tprintf("ReadConn slave \t");
            }
            local_result += execute_query(slave_conn[i], "select 1;");
        }
        if (galera_flag)
        {
            if (verbose)
            {
                tprintf("Galera \n");
            }
            local_result += execute_query(galera_conn[i], "select 1;");
        }
    }

    // global_result += check_pers_conn(Test, pers_conn_expected);
    tprintf("Closing all connections\n");
    for (i = 0; i < conn_N; i++)
    {
        set_timeout(20);
        if (rwsplit_flag)
        {
            mysql_close(rwsplit_conn[i]);
        }
        if (master_flag)
        {
            mysql_close(master_conn[i]);
        }
        if (slave_flag)
        {
            mysql_close(slave_conn[i]);
        }
        if (galera_flag)
        {
            mysql_close(galera_conn[i]);
        }
    }
    stop_timeout();

    return local_result;
}

int TestConnections::get_client_ip(int m, char* ip)
{
    MYSQL* conn;
    MYSQL_RES* res;
    MYSQL_ROW row;
    int ret = 1;
    unsigned long long int rows;
    unsigned long long int i;

    maxscales->connect_rwsplit(m);
    if (execute_query(maxscales->conn_rwsplit[m],
                      "CREATE DATABASE IF NOT EXISTS db_to_check_client_ip") != 0)
    {
        return ret;
    }
    maxscales->close_rwsplit(m);
    conn = open_conn_db(maxscales->rwsplit_port[m],
                        maxscales->IP[m],
                        (char*) "db_to_check_client_ip",
                        maxscales->user_name,
                        maxscales->password,
                        ssl);

    if (conn != NULL)
    {
        if (mysql_query(conn, "show processlist;") != 0)
        {
            printf("Error: can't execute SQL-query: show processlist\n");
            printf("%s\n\n", mysql_error(conn));
        }
        else
        {
            res = mysql_store_result(conn);
            if (res == NULL)
            {
                printf("Error: can't get the result description\n");
            }
            else
            {
                mysql_num_fields(res);
                rows = mysql_num_rows(res);
                for (i = 0; i < rows; i++)
                {
                    row = mysql_fetch_row(res);
                    if ((row[2] != NULL ) && (row[3] != NULL))
                    {
                        if (strstr(row[3], "db_to_check_client_ip") != NULL)
                        {
                            ret = 0;
                            strcpy(ip, row[2]);
                        }
                    }
                }
            }
            mysql_free_result(res);
        }
        execute_query(maxscales->conn_rwsplit[m], "DROP DATABASE db_to_check_client_ip");
    }

    mysql_close(conn);
    return ret;
}

int TestConnections::set_timeout(long int timeout_seconds)
{
    if (m_enable_timeouts)
    {
        timeout = timeout_seconds;
    }
    return 0;
}

int TestConnections::set_log_copy_interval(long int interval_seconds)
{
    log_copy_to_go = interval_seconds;
    log_copy_interval = interval_seconds;
    return 0;
}

int TestConnections::stop_timeout()
{
    timeout = 999999999;
    return 0;
}

void TestConnections::tprintf(const char* format, ...)
{
    timeval t2;
    gettimeofday(&t2, NULL);
    double elapsedTime = (t2.tv_sec - m_start_time.tv_sec);
    elapsedTime += (double) (t2.tv_usec - m_start_time.tv_usec) / 1000000.0;

    struct tm tm_now;
    localtime_r(&t2.tv_sec, &tm_now);
    unsigned int msec = t2.tv_usec / 1000;

    printf("%02u:%02u:%02u.%03u %04f: ", tm_now.tm_hour, tm_now.tm_min, tm_now.tm_sec, msec, elapsedTime);

    va_list argp;
    va_start(argp, format);
    vprintf(format, argp);
    va_end(argp);

    /** Add a newline if the message doesn't have one */
    if (format[strlen(format) - 1] != '\n')
    {
        printf("\n");
    }

    fflush(stdout);
    fflush(stderr);
}

int TestConnections::get_master_server_id(int m)
{
    int master_id = -1;
    MYSQL* conn = maxscales->open_rwsplit_connection(m);
    char str[100];
    if (find_field(conn, "SELECT @@server_id, @@last_insert_id;", "@@server_id", str) == 0)
    {
        char* endptr = NULL;
        auto colvalue = strtol(str, &endptr, 0);
        if (endptr && *endptr == '\0')
        {
            master_id = colvalue;
        }
    }
    mysql_close(conn);
    return master_id;
}
void* timeout_thread(void* ptr)
{
    TestConnections* Test = (TestConnections*) ptr;
    struct timespec tim;
    while (Test->timeout > 0)
    {
        tim.tv_sec = 1;
        tim.tv_nsec = 0;
        nanosleep(&tim, NULL);
        Test->timeout--;
    }
    Test->tprintf("\n **** Timeout! *** \n");
    Test->~TestConnections();
    exit(250);
}

void* log_copy_thread(void* ptr)
{
    TestConnections* Test = (TestConnections*) ptr;
    struct timespec tim;
    while (true)
    {
        while (Test->log_copy_to_go > 0)
        {
            tim.tv_sec = 1;
            tim.tv_nsec = 0;
            nanosleep(&tim, NULL);
            Test->log_copy_to_go--;
        }
        Test->log_copy_to_go = Test->log_copy_interval;
        Test->tprintf("\n **** Copying all logs *** \n");
        Test->copy_all_logs_periodic();
    }

    return NULL;
}

int TestConnections::insert_select(int m, int N)
{
    int result = 0;

    tprintf("Create t1\n");
    set_timeout(30);
    create_t1(maxscales->conn_rwsplit[m]);

    tprintf("Insert data into t1\n");
    set_timeout(N * 16 + 30);
    insert_into_t1(maxscales->conn_rwsplit[m], N);
    stop_timeout();
    repl->sync_slaves();

    tprintf("SELECT: rwsplitter\n");
    set_timeout(30);
    result += select_from_t1(maxscales->conn_rwsplit[m], N);

    tprintf("SELECT: master\n");
    set_timeout(30);
    result += select_from_t1(maxscales->conn_master[m], N);

    tprintf("SELECT: slave\n");
    set_timeout(30);
    result += select_from_t1(maxscales->conn_slave[m], N);

    return result;
}

int TestConnections::use_db(int m, char* db)
{
    int local_result = 0;
    char sql[100];

    sprintf(sql, "USE %s;", db);
    set_timeout(20);
    tprintf("selecting DB '%s' for rwsplit\n", db);
    local_result += execute_query(maxscales->conn_rwsplit[m], "%s", sql);
    tprintf("selecting DB '%s' for readconn master\n", db);
    local_result += execute_query(maxscales->conn_slave[m], "%s", sql);
    tprintf("selecting DB '%s' for readconn slave\n", db);
    local_result += execute_query(maxscales->conn_master[m], "%s", sql);
    for (int i = 0; i < repl->N; i++)
    {
        tprintf("selecting DB '%s' for direct connection to node %d\n", db, i);
        local_result += execute_query(repl->nodes[i], "%s", sql);
    }
    return local_result;
}

int TestConnections::check_t1_table(int m, bool presence, char* db)
{
    const char* expected = presence ? "" : "NOT";
    const char* actual = presence ? "NOT" : "";
    int start_result = global_result;

    add_result(use_db(m, db), "use db failed\n");
    stop_timeout();
    repl->sync_slaves();

    tprintf("Checking: table 't1' should %s be found in '%s' database\n", expected, db);
    set_timeout(30);
    int exists = check_if_t1_exists(maxscales->conn_rwsplit[m]);

    if (exists == presence)
    {
        tprintf("RWSplit: ok\n");
    }
    else
    {
        add_result(1, "Table t1 is %s found in '%s' database using RWSplit\n", actual, db);
    }

    set_timeout(30);
    exists = check_if_t1_exists(maxscales->conn_master[m]);

    if (exists == presence)
    {
        tprintf("ReadConn master: ok\n");
    }
    else
    {
        add_result(1,
                   "Table t1 is %s found in '%s' database using Readconnrouter with router option master\n",
                   actual,
                   db);
    }

    set_timeout(30);
    exists = check_if_t1_exists(maxscales->conn_slave[m]);

    if (exists == presence)
    {
        tprintf("ReadConn slave: ok\n");
    }
    else
    {
        add_result(1,
                   "Table t1 is %s found in '%s' database using Readconnrouter with router option slave\n",
                   actual,
                   db);
    }


    for (int i = 0; i < repl->N; i++)
    {
        set_timeout(30);
        exists = check_if_t1_exists(repl->nodes[i]);
        if (exists == presence)
        {
            tprintf("Node %d: ok\n", i);
        }
        else
        {
            add_result(1,
                       "Table t1 is %s found in '%s' database using direct connect to node %d\n",
                       actual,
                       db,
                       i);
        }
    }

    stop_timeout();

    return global_result - start_result;
}

int TestConnections::try_query(MYSQL* conn, const char* format, ...)
{
    va_list valist;

    va_start(valist, format);
    int message_len = vsnprintf(NULL, 0, format, valist);
    va_end(valist);

    char sql[message_len + 1];

    va_start(valist, format);
    vsnprintf(sql, sizeof(sql), format, valist);
    va_end(valist);

    int res = execute_query_silent(conn, sql, false);
    add_result(res,
               "Query '%.*s%s' failed!\n",
               message_len < 100 ? message_len : 100,
               sql,
               message_len < 100 ? "" : "...");
    return res;
}

int TestConnections::try_query_all(int m, const char* sql)
{
    return try_query(maxscales->conn_rwsplit[m], "%s", sql)
           + try_query(maxscales->conn_master[m], "%s", sql)
           + try_query(maxscales->conn_slave[m], "%s", sql);
}

StringSet TestConnections::get_server_status(const char* name)
{
    std::set<std::string> rval;
    int rc;
    char* res = maxscales->ssh_node_output_f(0, true, &rc, "maxadmin list servers|grep \'%s\'", name);
    char* pipe = strrchr(res, '|');

    if (res && pipe)
    {
        pipe++;
        char* tok = strtok(pipe, ",");

        while (tok)
        {
            char* p = tok;
            char* end = strchr(tok, '\n');
            if (!end)
            {
                end = strchr(tok, '\0');
            }

            // Trim leading whitespace
            while (p < end && isspace(*p))
            {
                p++;
            }

            // Trim trailing whitespace
            while (end > tok && isspace(*end))
            {
                *end-- = '\0';
            }

            rval.insert(p);
            tok = strtok(NULL, ",\n");
        }

        free(res);
    }

    return rval;
}

int TestConnections::list_dirs(int m)
{
    for (int i = 0; i < repl->N; i++)
    {
        tprintf("ls on node %d\n", i);
        repl->ssh_node(i, (char*) "ls -la /var/lib/mysql", true);
        fflush(stdout);
    }
    tprintf("ls maxscale \n");
    maxscales->ssh_node(m, "ls -la /var/lib/maxscale/", true);
    fflush(stdout);
    return 0;
}

void TestConnections::check_current_operations(int m, int value)
{
    char value_str[512];
    sprintf(value_str, "%d", value);

    for (int i = 0; i < repl->N; i++)
    {
        char command[512];
        sprintf(command, "show server server%d", i + 1);
        add_result(maxscales->check_maxadmin_param(m, command, "Current no. of operations:", value_str),
                   "Current no. of operations is not %s",
                   value_str);
    }
}

void TestConnections::check_current_connections(int m, int value)
{
    char value_str[512];
    sprintf(value_str, "%d", value);

    for (int i = 0; i < repl->N; i++)
    {
        char command[512];
        sprintf(command, "show server server%d", i + 1);
        add_result(maxscales->check_maxadmin_param(m, command, "Current no. of conns:", value_str),
                   "Current no. of conns is not %s",
                   value_str);
    }
}

int TestConnections::take_snapshot(char* snapshot_name)
{
    char str[m_take_snapshot_command.length() + strlen(snapshot_name) + 2];
    sprintf(str, "%s %s", m_take_snapshot_command.c_str(), snapshot_name);
    return system(str);
}

int TestConnections::revert_snapshot(char* snapshot_name)
{
    char str[m_revert_snapshot_command.length() + strlen(snapshot_name) + 2];
    sprintf(str, "%s %s", m_revert_snapshot_command.c_str(), snapshot_name);
    return system(str);
}

bool TestConnections::test_bad_config(int m, const string& config)
{
    process_template(m, config, "/tmp/");

    // Set the timeout to prevent hangs with configurations that work
    set_timeout(20);

    return maxscales->ssh_node_f(m,
                                 true,
                                 "cp /tmp/maxscale.cnf /etc/maxscale.cnf; pkill -9 maxscale; "
                                 "maxscale -U maxscale -lstdout &> /dev/null && sleep 1 && pkill -9 maxscale")
           == 0;
}
int TestConnections::call_mdbci(const char* options)
{
    struct stat buf;
    string filepath = m_mdbci_vm_path + "/" + m_mdbci_config_name;
    if (stat(filepath.c_str(), &buf))
    {
        if (process_mdbci_template())
        {
            tprintf("Failed to generate MDBCI virtual machines template");
            return 1;
        }
        if (system((std::string("mdbci --override --template ") + m_vm_path
                    + ".json generate " + m_mdbci_config_name).c_str()))
        {
            tprintf("MDBCI failed to generate virtual machines description");
            return 1;
        }
        if (system((std::string("cp -r ") + test_dir + std::string("/mdbci/cnf ")
                    + m_vm_path + "/").c_str()))
        {
            tprintf("Failed to copy my.cnf files");
            return 1;
        }
    }

    if (system((std::string("mdbci up ") + m_mdbci_config_name + " --labels " + m_mdbci_labels_str + " "
                + options).c_str()))
    {
        tprintf("MDBCI failed to bring up virtual machines");
        return 1;
    }

    std::string team_keys = envvar_get_set("team_keys", "~/.ssh/id_rsa.pub");
    string cmd = "mdbci public_keys --key " + team_keys + " " + m_mdbci_config_name;
    system(cmd.c_str());

    read_env();
    if (repl)
    {
        repl->read_basic_env();
    }
    if (galera)
    {
        galera->read_basic_env();
    }
    if (maxscales)
    {
        maxscales->read_basic_env();
    }
    return 0;
}

int TestConnections::process_mdbci_template()
{
    string box = envvar_get_set("box", "centos_7_libvirt");
    envvar_get_set("backend_box", "%s", box.c_str());
    envvar_get_set("target", "develop");
    envvar_get_set("vm_memory", "2048");

    string version = envvar_get_set("version", "10.3");
    envvar_get_set("galera_version", "%s", version.c_str());

    string product = envvar_get_set("product", "mariadb");
    string cnf_path;
    if (product == "mysql")
    {
        cnf_path = string_printf("%s/cnf/mysql56/", m_vm_path.c_str());
    }
    else
    {
        cnf_path = string_printf("%s/cnf/", m_vm_path.c_str());
    }
    setenv("cnf_path", cnf_path.c_str(), 1);

    string name = string(test_dir) + "/mdbci/templates/" + m_mdbci_template + ".json.template";
    string sys = string("envsubst < ") + name + " > " + m_vm_path + ".json";
    if (verbose)
    {
        std::cout << sys << std::endl;
    }
    return system(sys.c_str());
}

std::string dump_status(const StringSet& current, const StringSet& expected)
{
    std::stringstream ss;
    ss << "Current status: (";

    for (const auto& a : current)
    {
        ss << a << ",";
    }

    ss << ") Expected status: (";

    for (const auto& a : expected)
    {
        ss << a << ",";
    }

    ss << ")";

    return ss.str();
}
int TestConnections::reinstall_maxscales()
{
    char sys[m_target.length() + m_mdbci_config_name.length() + strlen(maxscales->prefix) + 70];
    for (int i = 0; i < maxscales->N; i++)
    {
        printf("Installing Maxscale on node %d\n", i);
        // TODO: make it via MDBCI and compatible with any distro
        maxscales->ssh_node(i, "yum remove maxscale -y", true);
        maxscales->ssh_node(i, "yum clean all", true);

        sprintf(sys, "mdbci install_product --product maxscale_ci --product-version %s %s/%s_%03d",
                m_target.c_str(), m_mdbci_config_name.c_str(), maxscales->prefix, i);
        if (system(sys))
        {
            return 1;
        }
    }
    return 0;
}

bool TestConnections::too_many_maxscales() const
{
    return maxscales->N < 2 && m_required_mdbci_labels.count(label_2nd_mxs) > 0;
}

std::string TestConnections::flatten_stringset(const StringSet& set)
{
    string rval;
    string sep;
    for (auto& elem : set)
    {
        rval += sep;
        rval += elem;
        sep = ",";
    }
    return rval;
}

StringSet TestConnections::parse_to_stringset(const string& source)
{
    string copy = source;
    StringSet rval;
    if (!copy.empty())
    {
        char* ptr = &copy[0];
        char* save_ptr = nullptr;
        // mdbci uses ',' and cmake uses ';'. Add ' ' as well to ensure trimming.
        const char delim[] = ",; ";
        char* token = strtok_r(ptr, delim, &save_ptr);
        while (token)
        {
            rval.insert(token);
            token = strtok_r(nullptr, delim, &save_ptr);
        }
    }
    return rval;
}

/**
 * MDBCI recognizes labels which affect backend configuration. Save those labels to a separate field.
 * Also save a string version.
 */
void TestConnections::set_mdbci_labels()
{
    StringSet mdbci_labels;
    mdbci_labels.insert("MAXSCALE");
    std::set_intersection(recognized_mdbci_labels.begin(), recognized_mdbci_labels.end(),
                          m_test_labels.begin(), m_test_labels.end(),
                          std::inserter(mdbci_labels, mdbci_labels.begin()));

    std::string mdbci_labels_str = flatten_stringset(mdbci_labels);
    if (TestConnections::verbose)
    {
        printf("mdbci-labels: %s\n", mdbci_labels_str.c_str());
    }
    m_required_mdbci_labels = mdbci_labels;
    m_mdbci_labels_str = mdbci_labels_str;
}
