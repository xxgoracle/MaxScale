#include <fstream>
#include <iostream>
#include <sstream>
#include "clusterix_nodes.h"

int Clusterix_nodes::install_clusterix(int m)
{
    int ec;
    char * clusterix_rpm = ssh_node_output(m, "rpm -qa | grep clustrix-clxnode", true, &ec);
    if (strstr(clusterix_rpm, "clustrix-clxnode") == NULL)
    {
        printf("%s\n", ssh_node_output(m, "rm /etc/yum.repos.d/epel.repo", true, &ec));
        printf("%s\n", ssh_node_output(m, CLUSTERIX_DEPS_YUM, true, &ec));
        printf("%s\n", ssh_node_output(m, WGET_CLUSTERIX, false, &ec));
        printf("%s\n", ssh_node_output(m, UNPACK_CLUSTERIX, false, &ec));
        printf("%s\n", ssh_node_output(m, INSTALL_CLUSTERIX, false, &ec));
        create_users(m);
   }
    return 0;
}

int Clusterix_nodes::start_cluster()
{
    for (int i = 0; i < N; i++)
    {
        install_clusterix(i);
    }
    std::string lic_filename = std::string(getenv("HOME")) +
            std::string("/.config/mdbci/clusterix_license");
    std::ifstream lic_file;
    lic_file.open(lic_filename.c_str());
    std::stringstream strStream;
    strStream << lic_file.rdbuf();
    std::string clusterix_license = strStream.str();
    lic_file.close();

    execute_query_all_nodes(clusterix_license.c_str());

    std::string cluster_setup_sql = std::string("ALTER CLUSTER ADD '") +
            std::string(IP_private[0]) +
            std::string("'");
    for (int i = 0; i < N; i++)
    {
        cluster_setup_sql += std::string(",'") +
                std::string(IP_private[i]) +
                std::string("'");
    }
    connect();
    execute_query(nodes[0], "%s", cluster_setup_sql.c_str());
    close_connections();
    return 0;
}
