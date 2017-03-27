/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#define MXS_MODULE_NAME "hintrouter"
#include "hintrouter.hh"

#include <limits>
#include <vector>

#include <maxscale/log_manager.h>
#include "dcb.hh"

static const MXS_ENUM_VALUE default_action_values[] =
{
    {"master", HINT_ROUTE_TO_MASTER},
    {"slave", HINT_ROUTE_TO_SLAVE},
    {"named", HINT_ROUTE_TO_NAMED_SERVER},
    {"all", HINT_ROUTE_TO_ALL},
    {NULL} /* Last must be NULL */
};
static const char DEFAULT_ACTION[] = "default_action";
static const char DEFAULT_SERVER[] = "default_server";
static const char MAX_SLAVES[] = "max_slaves";

HintRouter::HintRouter(SERVICE* pService, HINT_TYPE default_action, string& default_server,
                       int max_slaves)
    : maxscale::Router<HintRouter, HintRouterSession>(pService),
      m_routed_to_master(0),
      m_routed_to_slave(0),
      m_routed_to_named(0),
      m_routed_to_all(0),
      m_default_action(default_action),
      m_default_server(default_server),
      m_max_slaves(max_slaves),
      m_total_slave_conns(0)
{
    HR_ENTRY();
    if (m_max_slaves < 0)
    {
        // set a reasonable default value
        m_max_slaves = pService->n_dbref - 1;
    }
    MXS_NOTICE("Hint router [%s] created.", pService->name);
}

//static
HintRouter* HintRouter::create(SERVICE* pService, char** pzOptions)
{
    HR_ENTRY();

    MXS_CONFIG_PARAMETER* params = pService->svc_config_param;
    HINT_TYPE default_action = (HINT_TYPE)config_get_enum(params, DEFAULT_ACTION,
                                                          default_action_values);
    string default_server(config_get_string(params, DEFAULT_SERVER));
    int max_slaves = config_get_integer(params, MAX_SLAVES);
    return new HintRouter(pService, default_action, default_server, max_slaves);
}

HintRouterSession* HintRouter::newSession(MXS_SESSION *pSession)
{
    typedef HintRouterSession::RefArray::size_type array_index;
    HR_ENTRY();
    Dcb master_Dcb(NULL);
    HintRouterSession::BackendMap all_backends;
    all_backends.rehash(1 + m_max_slaves);
    HintRouterSession::BackendArray slave_arr;
    slave_arr.reserve(m_max_slaves);

    SERVER_REF* master_ref = NULL;
    HintRouterSession::RefArray slave_refs;
    slave_refs.reserve(m_max_slaves);

    /* Go through the server references, find master and slaves */
    for (SERVER_REF* pSref = pSession->service->dbref; pSref; pSref = pSref->next)
    {
        if (SERVER_REF_IS_ACTIVE(pSref))
        {
            if (SERVER_IS_MASTER(pSref->server))
            {
                if (!master_ref)
                {
                    master_ref = pSref;
                }
                else
                {
                    MXS_WARNING("Found multiple master servers when creating session.\n");
                }
            }
            else if (SERVER_IS_SLAVE(pSref->server))
            {
                slave_refs.push_back(pSref);
            }
        }
    }

    if (master_ref)
    {
        // Connect to master
        HR_DEBUG("Connecting to %s.", master_ref->server->unique_name);
        DCB* master_conn = dcb_connect(master_ref->server, pSession, master_ref->server->protocol);

        if (master_conn)
        {
            HR_DEBUG("Connected.");
            atomic_add(&master_ref->connections, 1);
            master_conn->service = pSession->service;

            master_Dcb = Dcb(master_conn);
            string name(master_conn->server->unique_name);
            all_backends.insert(HintRouterSession::MapElement(name, master_Dcb));
        }
        else
        {
            HR_DEBUG("Connection failed.");
        }
    }

    /* Different sessions may use different slaves if the 'max_session_slaves'-
     * setting is low enough. First, set maximal looping limits noting that the
     * array is treated as a ring. Also, array size may have changed since last
     * time it was formed. */
    if (slave_refs.size())
    {
        array_index size = slave_refs.size();
        array_index begin = m_total_slave_conns % size;
        array_index limit = begin + size;

        int slave_conns = 0;
        array_index current = begin;
        for (;
             (slave_conns < m_max_slaves) && current != limit;
             current++)
        {
            SERVER_REF* slave_ref = slave_refs.at(current % size);
            // Connect to a slave
            HR_DEBUG("Connecting to %s.", slave_ref->server->unique_name);
            DCB* slave_conn = dcb_connect(slave_ref->server, pSession, slave_ref->server->protocol);

            if (slave_conn)
            {
                HR_DEBUG("Connected.");
                atomic_add(&slave_ref->connections, 1);
                slave_conn->service = pSession->service;
                Dcb slave_Dcb(slave_conn);
                slave_arr.push_back(slave_Dcb);

                string name(slave_conn->server->unique_name);
                all_backends.insert(HintRouterSession::MapElement(name, slave_Dcb));
                slave_conns++;
            }
            else
            {
                HR_DEBUG("Connection failed.");
            }
        }
        m_total_slave_conns += slave_conns;
    }
    if (all_backends.size() != 0)
    {
        return new HintRouterSession(pSession, this, all_backends);
    }
    return NULL;
}

void HintRouter::diagnostics(DCB* pOut)
{
    HR_ENTRY();
    for (int i = 0; default_action_values[i].name; i++)
    {
        if (default_action_values[i].enum_value == m_default_action)
        {
            dcb_printf(pOut, "\tDefault action: route to %s\n", default_action_values[i].name);
        }
    }
    dcb_printf(pOut, "\tDefault server: %s\n", m_default_server.c_str());
    dcb_printf(pOut, "\tMaximum slave connections/session: %d\n", m_max_slaves);
    dcb_printf(pOut, "\tTotal cumulative slave connections: %d\n", m_total_slave_conns);
    dcb_printf(pOut, "\tQueries routed to master: %d\n", m_routed_to_master);
    dcb_printf(pOut, "\tQueries routed to single slave: %d\n", m_routed_to_slave);
    dcb_printf(pOut, "\tQueries routed to named server: %d\n", m_routed_to_named);
    dcb_printf(pOut, "\tQueries routed to all servers: %d\n", m_routed_to_all);
}

uint64_t HintRouter::getCapabilities()
{
    HR_ENTRY();
    return RCAP_TYPE_STMT_INPUT | RCAP_TYPE_RESULTSET_OUTPUT;
}

extern "C" MXS_MODULE* MXS_CREATE_MODULE()
{
    static MXS_MODULE module =
    {
        MXS_MODULE_API_ROUTER,   /* Module type */
        MXS_MODULE_BETA_RELEASE, /* Release status */
        MXS_ROUTER_VERSION,      /* Implemented module API version */
        "A hint router", /* Description */
        "V1.0.0", /* Module version */
        RCAP_TYPE_STMT_INPUT | RCAP_TYPE_RESULTSET_OUTPUT,
        &HintRouter::s_object,
        NULL, /* Process init, can be null */
        NULL, /* Process finish, can be null */
        NULL, /* Thread init */
        NULL, /* Thread finish */
        {
            {
                DEFAULT_ACTION,
                MXS_MODULE_PARAM_ENUM,
                default_action_values[0].name,
                MXS_MODULE_OPT_NONE,
                default_action_values
            },
            {DEFAULT_SERVER, MXS_MODULE_PARAM_SERVER, ""},
            {MAX_SLAVES, MXS_MODULE_PARAM_INT, "-1"},
            {MXS_END_MODULE_PARAMS}
        }
    };
    return &module;
}
