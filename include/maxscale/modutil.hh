/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-03-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

/**
 * @file modutil.hh A set of useful routines for module writers
 */

#include <maxscale/ccdefs.hh>
#include <string>
#include <maxscale/buffer.hh>
#include <maxscale/dcb.hh>
#include <maxscale/pcre2.hh>

#define PTR_IS_RESULTSET(b)    (b[0] == 0x01 && b[1] == 0x0 && b[2] == 0x0 && b[3] == 0x01)
#define PTR_IS_EOF(b)          (b[0] == 0x05 && b[1] == 0x0 && b[2] == 0x0 && b[4] == 0xfe)
#define PTR_IS_OK(b)           (b[4] == 0x00)
#define PTR_IS_ERR(b)          (b[4] == 0xff)
#define PTR_IS_LOCAL_INFILE(b) (b[4] == 0xfb)
#define IS_FULL_RESPONSE(buf)  (modutil_count_signal_packets(buf, 0, 0) == 2)

/** Static initialization define for modutil_state */
#define MODUTIL_STATE_INIT {0}

MXS_BEGIN_DECLS

extern int    modutil_is_SQL(GWBUF*);
extern int    modutil_is_SQL_prepare(GWBUF*);
extern int    modutil_extract_SQL(GWBUF*, char**, int*);
extern int    modutil_MySQL_Query(GWBUF*, char**, int*, int*);
extern char*  modutil_get_SQL(GWBUF*);
extern GWBUF* modutil_replace_SQL(GWBUF*, char*);
extern char*  modutil_get_query(GWBUF* buf);
extern int    modutil_send_mysql_err_packet(DCB*, int, int, int, const char*, const char*);
GWBUF*        modutil_get_next_MySQL_packet(GWBUF** p_readbuf);
GWBUF*        modutil_get_complete_packets(GWBUF** p_readbuf);
int           modutil_MySQL_query_len(GWBUF* buf, int* nbytes_missing);
int           modutil_count_statements(GWBUF* buffer);
int           modutil_count_packets(GWBUF* buffer);

GWBUF* modutil_create_query(const char* query);
GWBUF* modutil_create_mysql_err_msg(int packet_number, int affected_rows, int merrno,
                                    const char* statemsg, const char* msg);
GWBUF* modutil_create_ok();
GWBUF* modutil_create_eof(uint8_t sequence);

/** Struct used for tracking the state inside the modutil functions */
typedef struct
{
    uint8_t state;
} modutil_state;

/**
 * @brief Count the number of EOF and ERR packets in the buffer.
 *
 * Only complete packets are inspected and the buffer is assumed to only contain
 * whole packets. If partial packets are in the buffer, they are ignored.
 * The caller must handle the detection of partial packets in buffers.
 *
 * Before the first invocation, the value pointed by the @c state parameter
 * should be initialized with MODUTIL_STATE_INIT. All subsequent calls with a
 * partially processed result set must be made with only unprocessed packets
 * in @c reply.
 *
 * @param reply      Buffer to use
 * @param n_found    Number of previous found packets
 * @param more       Set to true if more results exist
 * @param state      Internal state of the function, NULL if the function is
 *                   only called once per result set
 *
 * @return Total number of EOF and ERR packets including the ones already found
 */
int modutil_count_signal_packets(GWBUF* reply, int n_found, bool* more, modutil_state* state);

mxs_pcre2_result_t modutil_mysql_wildcard_match(const char* pattern, const char* string);

/**
 * Given a buffer containing a MySQL statement, this function will return
 * a pointer to the first character that is not whitespace. In this context,
 * comments are also counted as whitespace. For instance:
 *
 *    "SELECT"                    => "SELECT"
 *    "  SELECT                   => "SELECT"
 *    " / * A comment * / SELECT" => "SELECT"
 *    "-- comment\nSELECT"        => "SELECT"
 *
 *  @param sql  Pointer to buffer containing a MySQL statement
 *  @param len  Length of sql.
 *
 *  @return The first non whitespace (including comments) character. If the
 *          entire buffer is only whitespace, the returned pointer will point
 *          to the character following the buffer (i.e. sql + len).
 */
char* modutil_MySQL_bypass_whitespace(char* sql, size_t len);

/**
 * Creates a COM_PING that does not return a result
 */
GWBUF* modutil_create_ignorable_ping();

/** Character and token searching functions */
char* strnchr_esc(char* ptr, char c, int len);
char* strnchr_esc_mysql(char* ptr, char c, int len);
bool  is_mysql_statement_end(const char* start, int len);
bool  is_mysql_sp_end(const char* start, int len);
char* modutil_get_canonical(GWBUF* querybuf);

// TODO: Move modutil out of the core
const char* STRPACKETTYPE(int p);

MXS_END_DECLS

namespace maxscale
{

/**
 * Extract SQL from buffer
 *
 * @param buffer Buffer containing an SQL statement
 * @param len    Maximum length of the returned string, no limit by default
 *
 * @return The SQL statement. If the buffer does not contain a SQL statement, an empty string is returned.
 */
std::string extract_sql(GWBUF* buffer, size_t len = -1);

/**
 * Extract error messages from buffers
 *
 * @param buffer Buffer containing an error
 *
 * @return String representation of the error
 */
std::string extract_error(GWBUF* buffer);

/**
 * Get canonical form of a query
 *
 * The canonical form of a query has all constants replaced with question marks. This form is the "signature"
 * of the query and it can be used to group similar queries together.
 *
 * @param buffer Buffer containing a SQL statement
 *
 * @return The canonical form of the query
 */
std::string get_canonical(GWBUF* buffer);

/**
 * Truncate buffers at packet boundaries
 *
 * @param b   Buffer to truncate
 * @param pkt Upper limit of how many packets to return
 *
 * @return A buffer with at most `ptk` packets in it
 */
GWBUF* truncate_packets(GWBUF* b, uint64_t pkt);
}
