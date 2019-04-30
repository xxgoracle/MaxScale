#include "clusterix_nodes.h"

int Clusterix_nodes::install_clusterix(int m)
{
    ssh_node(m, CLUSTERIX_DEPS_YUM, true);
    ssh_node(m, WGET_CLUSTERIX, false);
    ssh_node(m, UNPACK_CLUSTERIX, false);
    ssh_node(m, INSTALL_CLUSTERIX, false);
    create_users(0);
    return 0;
}
