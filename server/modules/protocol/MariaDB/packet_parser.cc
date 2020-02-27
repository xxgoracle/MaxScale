/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "packet_parser.hh"
#include <maxsql/mariadb.hh>
#include <maxscale/protocol/mariadb/mysql.hh>

using std::string;

namespace
{
void pop_front(packet_parser::ByteVec& data, int len)
{
    auto begin = data.begin();
    data.erase(begin, begin + len);
}

auto read_stringz_if_cap(packet_parser::ByteVec& data, uint32_t client_caps, uint32_t req_caps)
{
    std::pair<bool, string> rval(true, "");     // success & result
    if ((client_caps & req_caps) == req_caps)
    {
        if (!data.empty())
        {
            rval.second = (const char*)data.data();
            pop_front(data, rval.second.size() + 1);    // Should be null-terminated.
        }
        else
        {
            rval.first = false;
        }
    }
    return rval;
}
}
namespace packet_parser
{


ClientInfo parse_client_capabilities(ByteVec& data, const ClientInfo* old_info)
{
    auto rval = old_info ? *old_info : ClientInfo();

    // Can assume that client capabilities are in the first 32 bytes and the buffer is large enough.
    const uint8_t* ptr = data.data();
    /**
     * We OR the capability bits in order to retain the starting bits sent
     * when an SSL connection is opened. Oracle Connector/J 8.0 appears to drop
     * the SSL capability bit mid-authentication which causes MaxScale to think
     * that SSL is not used.
     */
    rval.m_client_capabilities |= mariadb::get_byte4(ptr);
    ptr += 4;

    // Next is max packet size, skip it.
    ptr += 4;

    rval.m_charset = *ptr;
    ptr += 1;

    // Next, 19 bytes of reserved filler. Skip.
    ptr += 19;

    /**
     * Next, 4 bytes of extra capabilities. Not always used.
     * MariaDB 10.2 compatible clients don't set the first bit to signal that
     * there are extra capabilities stored in the last 4 bytes of the filler.
     */
    if ((rval.m_client_capabilities & GW_MYSQL_CAPABILITIES_CLIENT_MYSQL) == 0)
    {
        // We don't support COM_MULTI or progress reporting. The former is not used and the latter requires
        // some extra work to implement correctly.
        rval.m_extra_capabilities |= (mariadb::get_byte4(ptr) & MXS_EXTRA_CAPABILITIES_SERVER);
    }
    ptr += 4;
    pop_front(data, ptr - data.data());
    return rval;
}

ClientResponseResult parse_client_response(ByteVec& data, uint32_t client_caps)
{
    ClientResponseResult rval;
    // A null-terminated username should be first. Cannot overrun since caller added 0 to end of buffer.
    rval.username = (const char*)data.data();
    pop_front(data, rval.username.size() + 1);

    // Next is authentication response. The length is encoded in different forms depending on
    // capabilities.
    rval.token_res = parse_auth_token(data, client_caps, AuthPacketType::HANDSHAKE_RESPONSE);
    if (rval.token_res.success)
    {
        // The following fields are optional.
        auto db_res = read_stringz_if_cap(data, client_caps, GW_MYSQL_CAPABILITIES_CONNECT_WITH_DB);
        auto plugin_res = read_stringz_if_cap(data, client_caps, GW_MYSQL_CAPABILITIES_PLUGIN_AUTH);
        if (db_res.first && plugin_res.first)
        {
            rval.db = std::move(db_res.second);
            rval.plugin = std::move(plugin_res.second);

            rval.attr_res = parse_attributes(data, client_caps);
            if (rval.attr_res.success)
            {
                rval.success = true;
            }
        }
    }
    return rval;
}

AuthParseResult parse_auth_token(ByteVec& data, uint32_t client_caps, AuthPacketType packet_type)
{
    AuthParseResult rval;
    if (data.empty())
    {
        return rval;
    }

    // The length is encoded in different forms depending on capabilities and packet type.
    const uint8_t* ptr = data.data();
    bool error = false;
    uint64_t len_remaining = data.size();
    uint64_t auth_token_len_bytes = 0;  // In how many bytes the auth token length is encoded in.
    uint64_t auth_token_len = 0;        // The actual auth token length.

    // com_change_user does not support the length-encoded token.
    if (packet_type == AuthPacketType::HANDSHAKE_RESPONSE
        && client_caps & GW_MYSQL_CAPABILITIES_AUTH_LENENC_DATA)
    {
        // Token is a length-encoded string. First is a length-encoded integer, then the token data.
        auth_token_len_bytes = mxq::leint_bytes(ptr);
        if (auth_token_len_bytes <= len_remaining)
        {
            auth_token_len = mxq::leint_value(ptr);
        }
        else
        {
            error = true;
        }
    }
    else if (client_caps & GW_MYSQL_CAPABILITIES_SECURE_CONNECTION)
    {
        // First token length 1 byte, then token data.
        auth_token_len_bytes = 1;
        auth_token_len = *ptr;
    }
    else
    {
        // unsupported client version
        rval.old_protocol = true;
        error = true;
    }

    if (!error)
    {
        auto total_len = auth_token_len_bytes + auth_token_len;
        if (total_len <= len_remaining)
        {
            rval.success = true;
            ptr += auth_token_len_bytes;
            if (auth_token_len > 0)
            {
                rval.auth_token.assign(ptr, ptr + auth_token_len);
            }
            pop_front(data, total_len);
        }
    }
    return rval;
}

AttrParseResult parse_attributes(ByteVec& data, uint32_t client_caps)
{
    // The data is not processed into key-value pairs as it is simply fed to backends as is.
    AttrParseResult rval;
    if (data.empty())
    {
        return rval;
    }

    auto len_remaining = data.size();

    if (client_caps & GW_MYSQL_CAPABILITIES_CONNECT_ATTRS)
    {
        if (len_remaining > 0)
        {
            const auto ptr = data.data();
            auto leint_len = mxq::leint_bytes(ptr);
            if (leint_len <= len_remaining)
            {
                auto attr_len = mxq::leint_value(ptr);
                auto total_attr_len = leint_len + attr_len;
                if (total_attr_len <= len_remaining)
                {
                    rval.success = true;
                    rval.attr_data.assign(ptr, ptr + total_attr_len);
                    pop_front(data, total_attr_len);
                }
            }
        }
    }
    else
    {
        rval.success = true;
    }
    return rval;
}

ChangeUserParseResult parse_change_user_packet(ByteVec& data, uint32_t client_caps)
{
    ChangeUserParseResult rval;
    const uint8_t* ptr = data.data();

    mxb_assert(*ptr == MXS_COM_CHANGE_USER);
    ptr++;

    // null-terminated username. Again, cannot overflow.
    rval.username = (const char*)ptr;
    ptr += rval.username.length() + 1;
    pop_front(data, ptr - data.data());

    rval.token_res = parse_auth_token(data, client_caps, AuthPacketType::COM_CHANGE_USER);
    if (rval.token_res.success)
    {
        auto db_res = read_stringz_if_cap(data, client_caps, GW_MYSQL_CAPABILITIES_CONNECT_WITH_DB);
        if (db_res.first)
        {
            rval.db = std::move(db_res.second);
            // charset, 2 bytes
            if (data.size() >= 2)
            {
                rval.charset = mariadb::get_byte2(data.data());
                pop_front(data, 2);
                // new auth plugin
                auto plugin_res = read_stringz_if_cap(data, client_caps, GW_MYSQL_CAPABILITIES_PLUGIN_AUTH);
                if (plugin_res.first)
                {
                    rval.plugin = std::move(plugin_res.second);
                    // finally, connection attributes
                    rval.attr_res = parse_attributes(data, client_caps);
                    if (rval.attr_res.success)
                    {
                        rval.success = true;
                    }
                }
            }
        }
    }
    return rval;
}

mariadb::AuthSwitchReqContents parse_auth_switch_request(ByteVec& data)
{
    mariadb::AuthSwitchReqContents rval;
    // The data should have at least a cmd-byte, plugin name and plugin data.
    const int minlen = 3;
    if (data.size() >= minlen)
    {
        const uint8_t* ptr = data.data();
        const uint8_t* end = ptr + data.size();

        if (*ptr == MYSQL_REPLY_AUTHSWITCHREQUEST)
        {
            ptr++;
            // Next, null-terminated plugin name. Check for invalid string.
            size_t len_remaining = end - ptr;
            size_t plugin_name_len = strnlen((const char*)ptr, len_remaining);
            // The length should be smaller than total length.
            if (plugin_name_len > 0 && plugin_name_len < len_remaining)
            {
                rval.plugin_name = (const char*)ptr;
                ptr += rval.plugin_name.length() + 1;

                // Next plugin data until the end.
                if (ptr < end)
                {
                    rval.plugin_data.assign(ptr, end);
                    rval.success = true;
                }
            }
        }
    }
    return rval;
}
}