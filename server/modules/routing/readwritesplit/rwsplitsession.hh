#pragma once
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2020-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "readwritesplit.hh"
#include "rwsplit_ps.hh"
#include "rwbackend.hh"
#include "routeinfo.hh"

#include <string>

#include <maxscale/modutil.h>

typedef enum
{
    EXPECTING_NOTHING = 0,
    EXPECTING_WAIT_GTID_RESULT,
    EXPECTING_REAL_RESULT
} wait_gtid_state_t;

typedef std::map<uint32_t, uint32_t> ClientHandleMap;  /** External ID to internal ID */

typedef std::tr1::unordered_set<std::string> TableSet;
typedef std::map<uint64_t, uint8_t>          ResponseMap;

/** List of slave responses that arrived before the master */
typedef std::list< std::pair<mxs::SRWBackend, uint8_t> > SlaveResponseList;

/** Map of COM_STMT_EXECUTE targets by internal ID */
typedef std::tr1::unordered_map<uint32_t, mxs::SRWBackend> ExecMap;

/**
 * The client session of a RWSplit instance
 */
class RWSplitSession: public mxs::RouterSession
{
    RWSplitSession(const RWSplitSession&) = delete;
    RWSplitSession& operator=(const RWSplitSession&) = delete;

public:

    /**
     * Create a new router session
     *
     * @param instance Router instance
     * @param session  The session object
     *
     * @return New router session
     */
    static RWSplitSession* create(RWSplit* router, MXS_SESSION* session);

    /**
     * Called when a client session has been closed.
     */
    void close();

    /**
     * Called when a packet being is routed to the backend. The router should
     * forward the packet to the appropriate server(s).
     *
     * @param pPacket A client packet.
     */
    int32_t routeQuery(GWBUF* pPacket);

    /**
     * Called when a packet is routed to the client. The router should
     * forward the packet to the client using `MXS_SESSION_ROUTE_REPLY`.
     *
     * @param pPacket  A client packet.
     * @param pBackend The backend the packet is coming from.
     */
    void clientReply(GWBUF* pPacket, DCB* pBackend);

    /**
     *
     * @param pMessage  The error message.
     * @param pProblem  The DCB on which the error occurred.
     * @param action    The context.
     * @param pSuccess  On output, if false, the session will be terminated.
     */
    void handleError(GWBUF*             pMessage,
                     DCB*               pProblem,
                     mxs_error_action_t action,
                     bool*              pSuccess);

    // TODO: Make member variables private
    mxs::SRWBackendList     backends; /**< List of backend servers */
    mxs::SRWBackend         current_master; /**< Current master server */
    mxs::SRWBackend         target_node; /**< The currently locked target node */
    mxs::SRWBackend         prev_target; /**< The previous target where a query was sent */
    bool                    large_query; /**< Set to true when processing payloads >= 2^24 bytes */
    Config                  rses_config; /**< copied config info from router instance */
    int                     rses_nbackends;
    enum ld_state           load_data_state; /**< Current load data state */
    bool                    have_tmp_tables;
    uint64_t                rses_load_data_sent; /**< How much data has been sent */
    DCB*                    client_dcb;
    uint64_t                sescmd_count;
    int                     expected_responses; /**< Number of expected responses to the current query */
    GWBUF*                  query_queue; /**< Queued commands waiting to be executed */
    RWSplit*                router; /**< The router instance */
    TableSet                temp_tables; /**< Set of temporary tables */
    mxs::SessionCommandList sescmd_list; /**< List of executed session commands */
    ResponseMap             sescmd_responses; /**< Response to each session command */
    SlaveResponseList       slave_responses; /**< Slaves that replied before the master */
    uint64_t                sent_sescmd; /**< ID of the last sent session command*/
    uint64_t                recv_sescmd; /**< ID of the most recently completed session command */
    PSManager               ps_manager;  /**< Prepared statement manager*/
    ClientHandleMap         ps_handles;  /**< Client PS handle to internal ID mapping */
    ExecMap                 exec_map; /**< Map of COM_STMT_EXECUTE statement IDs to Backends */
    std::string             gtid_pos; /**< Gtid position for causal read */
    wait_gtid_state_t       wait_gtid_state; /**< Determine boundray of wait gtid result and client query result */
    uint32_t                next_seq; /**< Next packet'ssequence number */

private:
    RWSplitSession(RWSplit* instance, MXS_SESSION* session,
                   const mxs::SRWBackendList& backends, const mxs::SRWBackend& master);

    void process_sescmd_response(mxs::SRWBackend& backend, GWBUF** ppPacket);
    void purge_history(mxs::SSessionCommand& sescmd);

    bool route_session_write(GWBUF *querybuf, uint8_t command, uint32_t type);
    bool route_single_stmt(GWBUF *querybuf, const RouteInfo& info);
    bool route_stored_query();
    bool reroute_stored_statement(const mxs::SRWBackend& old, GWBUF *stored);

    mxs::SRWBackend get_hinted_backend(char *name);
    mxs::SRWBackend get_slave_backend(int max_rlag);
    mxs::SRWBackend get_master_backend();
    mxs::SRWBackend get_target_backend(backend_type_t btype, char *name, int max_rlag);

    bool handle_target_is_all(route_target_t route_target, GWBUF *querybuf,
                              int packet_type, uint32_t qtype);
    mxs::SRWBackend handle_hinted_target(GWBUF *querybuf, route_target_t route_target);
    mxs::SRWBackend handle_slave_is_target(uint8_t cmd, uint32_t stmt_id);
    bool handle_master_is_target(mxs::SRWBackend* dest);
    bool handle_got_target(GWBUF* querybuf, mxs::SRWBackend& target, bool store);
    void handle_connection_keepalive(mxs::SRWBackend& target);
    bool prepare_target(mxs::SRWBackend& target, route_target_t route_target);

    bool should_replace_master(mxs::SRWBackend& target);
    void replace_master(mxs::SRWBackend& target);
    void log_master_routing_failure(bool found, mxs::SRWBackend& old_master,
                                    mxs::SRWBackend& curr_master);

    GWBUF* add_prefix_wait_gtid(SERVER *server, GWBUF *origin);
    void correct_packet_sequence(GWBUF *buffer);
    GWBUF* discard_master_wait_gtid_result(GWBUF *buffer);

    int get_max_replication_lag();
    mxs::SRWBackend& get_backend_from_dcb(DCB *dcb);

    void handle_error_reply_client(DCB *backend_dcb, GWBUF *errmsg);
    bool handle_error_new_connection(DCB *backend_dcb, GWBUF *errmsg);

    /**
     * Check if the session is locked to the master
     *
     * @return Whether the session is locked to the master
     */
    inline bool locked_to_master() const
    {
        return large_query || (current_master && target_node == current_master);
    }
};

/**
 * @brief Get the internal ID for the given binary prepared statement
 *
 * @param rses   Router client session
 * @param buffer Buffer containing a binary protocol statement other than COM_STMT_PREPARE
 *
 * @return The internal ID of the prepared statement that the buffer contents refer to
 */
uint32_t get_internal_ps_id(RWSplitSession* rses, GWBUF* buffer);
