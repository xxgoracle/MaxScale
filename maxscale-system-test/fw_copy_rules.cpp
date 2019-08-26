#include "fw_copy_rules.h"
#include <sstream>

void copy_rules(TestConnections* Test, const char* rules_name, const char* rules_dir)
{
    std::stringstream src;
    std::stringstream dest;
    std::stringstream cmd;

    src << rules_dir << "/" << rules_name;
    dest << Test->maxscales->access_homedir[0] << "/rules/rules.txt";

    if (Test->docker_backend)
    {
        Test->set_timeout(120);
        cmd << "mdbci provide-files " << Test->mdbci_config_name << "/" << Test->maxscales->prefix << "_000 " << src.str() << ":" << "/rules/rules.txt";
        system(cmd.str().c_str());
    }
    else
    {
        Test->set_timeout(30);
        Test->maxscales->copy_to_node_legacy(src.str().c_str(), dest.str().c_str(), 0);
    }
    Test->stop_timeout();
}

void copy_modified_rules(TestConnections* Test, const char* rules_name, const char* rules_dir, const char* sed_cmd)
{
    std::stringstream src;
    std::stringstream cmd;
    src << rules_dir << "/" << rules_name;
    cmd << "cp " << src.str() << " .";
    system(cmd.str().c_str());
    std::stringstream sed;
    sed << sed_cmd << rules_name;
    system(sed.str().c_str());
    copy_rules(Test, rules_name, ".");
}
