#pragma once

#include <errno.h>
#include <string>
#include "nodes.h"
#include "mariadb_nodes.h"

#define CLUSTRIX_DEPS_YUM "yum install -y bzip2 wget screen ntp ntpdate vim htop mdadm"
#define WGET_CLUSTRIX "wget http://files.clustrix.com/releases/software/clustrix-9.1.4.el7.tar.bz2"
#define UNPACK_CLUSTRIX "tar xvjf clustrix-9.1.4.el7.tar.bz2"
#define INSTALL_CLUSTRIX "cd clustrix-9.1.4.el7; sudo ./clxnode_install.py --yes --force"

class Clustrix_nodes : public Mariadb_nodes
{
public:

     Clustrix_nodes(const char *pref, const char *test_cwd, bool verbose, std::string network_config) :
        Mariadb_nodes(pref, test_cwd, verbose, network_config) { }

     int install_clustrix(int m);
     int start_cluster();
     std::string cnf_servers();
};
