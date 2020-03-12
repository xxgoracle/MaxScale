/*
 * Copyright (c) 2016 MariaDB Corporation Ab
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
#include <maxscale/ccdefs.hh>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <crypt.h>
#include <sys/stat.h>
#include <string>

#include <maxscale/adminusers.hh>
#include <maxbase/alloc.h>
#include <maxscale/cn_strings.hh>
#include <maxscale/users.hh>
#include <maxbase/pam_utils.hh>
#include <maxscale/paths.h>
#include <maxscale/json_api.hh>
#include <maxscale/utils.hh>
#include <maxscale/event.hh>

/**
 * @file adminusers.c - Administration user account management
 */

using mxs::Users;
using mxs::user_account_type;
using mxs::USER_ACCOUNT_UNKNOWN;
using mxs::USER_ACCOUNT_ADMIN;
using mxs::USER_ACCOUNT_BASIC;

namespace
{
Users linux_users;
Users inet_users;

bool load_users(const char* fname, Users* output);

const char LINUX_USERS_FILE_NAME[] = "maxadmin-users";
const char INET_USERS_FILE_NAME[] = "passwd";
const int LINELEN = 80;
}

/**
 * Admin Users initialisation
 */
void admin_users_init()
{
    if (!load_users(LINUX_USERS_FILE_NAME, &linux_users))
    {
        admin_enable_linux_account(DEFAULT_ADMIN_USER, USER_ACCOUNT_ADMIN);
    }

    if (!load_users(INET_USERS_FILE_NAME, &inet_users))
    {
        admin_add_inet_user(INET_DEFAULT_USERNAME, INET_DEFAULT_PASSWORD, USER_ACCOUNT_ADMIN);
    }
}

namespace
{
bool admin_dump_users(const Users* users, const char* fname)
{
    if (access(get_datadir(), F_OK) != 0)
    {
        if (mkdir(get_datadir(), S_IRWXU) != 0 && errno != EEXIST)
        {
            MXS_ERROR("Failed to create directory '%s': %d, %s",
                      get_datadir(),
                      errno,
                      mxs_strerror(errno));
            return false;
        }
    }

    bool rval = false;
    std::string path = std::string(get_datadir()) + "/" + fname;
    std::string tmppath = path + ".tmp";

    int fd = open(tmppath.c_str(), O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);

    if (fd == -1)
    {
        MXS_ERROR("Failed to create '%s': %d, %s", tmppath.c_str(), errno, mxs_strerror(errno));
    }
    else
    {
        json_t* json = users->to_json();
        char* str = json_dumps(json, 0);
        json_decref(json);

        if (write(fd, str, strlen(str)) == -1)
        {
            MXS_ERROR("Failed to dump admin users to '%s': %d, %s",
                      tmppath.c_str(), errno, mxs_strerror(errno));
        }
        else if (rename(tmppath.c_str(), path.c_str()) == -1)
        {
            MXS_ERROR("Failed to rename to '%s': %d, %s",
                      path.c_str(), errno, mxs_strerror(errno));
        }
        else
        {
            rval = true;
        }

        MXS_FREE(str);
        close(fd);
    }

    return rval;
}

const char* admin_add_user(Users* pusers, const char* fname, const char* uname, const char* password,
                           user_account_type type)
{
    if (!pusers->add(uname, password ? password : "", type))
    {
        return ADMIN_ERR_DUPLICATE;
    }

    if (!admin_dump_users(pusers, fname))
    {
        return ADMIN_ERR_FILEOPEN;
    }

    return ADMIN_SUCCESS;
}

const char* admin_alter_user(Users* pusers, const char* fname, const char* uname, const char* password)
{
    if (!users_change_password(pusers, uname, password))
    {
        return ADMIN_ERR_USERNOTFOUND;
    }

    if (!admin_dump_users(pusers, fname))
    {
        return ADMIN_ERR_FILEOPEN;
    }

    return ADMIN_SUCCESS;
}

const char* admin_remove_user(Users* users, const char* fname, const char* uname)
{
    if (!users->remove(uname))
    {
        MXS_ERROR("Couldn't find user %s. Removing user failed.", uname);
        return ADMIN_ERR_USERNOTFOUND;
    }

    if (!admin_dump_users(users, fname))
    {
        return ADMIN_ERR_FILEOPEN;
    }

    return ADMIN_SUCCESS;
}
}
static json_t* admin_user_json_data(const char* host,
                                    const char* user,
                                    enum user_type user_type,
                                    enum user_account_type account)
{
    mxb_assert(user_type != USER_TYPE_ALL);
    const char* type = user_type == USER_TYPE_INET ? CN_INET : CN_UNIX;

    json_t* entry = json_object();
    json_object_set_new(entry, CN_ID, json_string(user));
    json_object_set_new(entry, CN_TYPE, json_string(type));

    json_t* param = json_object();
    json_object_set_new(param, CN_ACCOUNT, json_string(account_type_to_str(account)));
    json_object_set_new(entry, CN_ATTRIBUTES, param);

    std::string self = MXS_JSON_API_USERS;
    self += type;
    json_object_set_new(entry, CN_RELATIONSHIPS, mxs_json_self_link(host, self.c_str(), user));

    return entry;
}

namespace
{
void user_types_to_json(Users* users, json_t* arr, const char* host, user_type type)
{
    json_t* json = users->diagnostics();
    size_t index;
    json_t* value;

    json_array_foreach(json, index, value)
    {
        const char* user = json_string_value(json_object_get(value, CN_NAME));
        enum user_account_type account = json_to_account_type(json_object_get(value, CN_ACCOUNT));
        json_array_append_new(arr, admin_user_json_data(host, user, type, account));
    }

    json_decref(json);
}
}

static std::string path_from_type(enum user_type type)
{
    std::string path = MXS_JSON_API_USERS;

    if (type == USER_TYPE_INET)
    {
        path += CN_INET;
    }
    else if (type == USER_TYPE_UNIX)
    {
        path += CN_UNIX;
    }

    return path;
}

json_t* admin_user_to_json(const char* host, const char* user, enum user_type type)
{
    user_account_type account = USER_ACCOUNT_BASIC;
    if ((type == USER_TYPE_INET && admin_user_is_inet_admin(user, nullptr))
        || (type == USER_TYPE_UNIX && admin_user_is_unix_admin(user)))
    {
        account = USER_ACCOUNT_ADMIN;
    }

    std::string path = path_from_type(type);
    path += "/";
    path += user;

    return mxs_json_resource(host, path.c_str(), admin_user_json_data(host, user, type, account));
}

json_t* admin_all_users_to_json(const char* host, enum user_type type)
{
    json_t* arr = json_array();
    std::string path = path_from_type(type);

    if (!inet_users.empty() && (type == USER_TYPE_ALL || type == USER_TYPE_INET))
    {
        user_types_to_json(&inet_users, arr, host, USER_TYPE_INET);
    }

    if (!linux_users.empty() && (type == USER_TYPE_ALL || type == USER_TYPE_UNIX))
    {
        user_types_to_json(&linux_users, arr, host, USER_TYPE_UNIX);
    }

    return mxs_json_resource(host, path.c_str(), arr);
}

namespace
{

bool load_legacy_users(FILE* fp, Users* output)
{
    Users rval;
    char path[PATH_MAX];
    char uname[80];
    bool error = false;

    while (fgets(uname, sizeof(uname), fp))
    {
        char* nl = strchr(uname, '\n');
        if (nl)
        {
            *nl = '\0';
        }
        else if (!feof(fp))
        {
            MXS_ERROR("Line length exceeds %d characters, possibly corrupted 'passwd' file in: %s",
                      LINELEN, path);
            error = true;
            break;
        }

        const char* password;
        char* colon = strchr(uname, ':');
        if (colon)
        {
            // Inet case
            *colon = 0;
            password = colon + 1;
        }
        else
        {
            // Linux case.
            password = "";
        }

        rval.add(uname, password, USER_ACCOUNT_ADMIN);
    }

    if (!error)
    {
        *output = std::move(rval);
    }
    return !error;
}

/**
 * Load the admin users.
 *
 * @param fname Name of the file in the datadir to load
 * @param output Result output
 * @return True on success
 */
bool load_users(const char* fname, Users* output)
{
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/%s", get_datadir(), fname);

    FILE* fp = fopen(path, "r");
    if (fp)
    {
        json_error_t err;
        json_t* json = json_loadf(fp, 0, &err);
        if (json)
        {
            /** New format users */
            output->load_json(json);
            json_decref(json);
        }
        else
        {
            /** Old style users file */
            if (load_legacy_users(fp, output))
            {
                /** Users loaded successfully, back up the original file and
                 * replace it with the new one */
                const char backup_suffix[] = ".backup";
                char newpath[strlen(path) + sizeof(backup_suffix)];
                sprintf(newpath, "%s%s", path, backup_suffix);

                if (rename(path, newpath) != 0)
                {
                    MXS_ERROR("Failed to rename old users file: %d, %s",
                              errno,
                              mxs_strerror(errno));
                }
                else if (!admin_dump_users(output, fname))
                {
                    MXS_ERROR("Failed to dump new users. Please rename the file "
                              "'%s' manually to '%s' and restart MaxScale to "
                              "attempt again.",
                              newpath,
                              path);
                }
                else
                {
                    MXS_NOTICE("Upgraded users file at '%s' to new format, "
                               "backup of the old file is stored in '%s'.",
                               newpath,
                               path);
                }
            }
        }

        fclose(fp);
        return true;
    }
    return false;
}
}

/**
 * Enable Linux account
 *
 * @param uname Name of Linux user
 *
 * @return NULL on success or an error string on failure.
 */
const char* admin_enable_linux_account(const char* uname, enum user_account_type type)
{
    return admin_add_user(&linux_users, LINUX_USERS_FILE_NAME, uname, NULL, type);
}

/**
 * Disable Linux account
 *
 * @param uname Name of Linux user
 *
 * @return NULL on success or an error string on failure.
 */
const char* admin_disable_linux_account(const char* uname)
{
    return admin_remove_user(&linux_users, LINUX_USERS_FILE_NAME, uname);
}

/**
 * Check whether Linux account is enabled
 *
 * @param uname The user name
 *
 * @return True if the account is enabled, false otherwise.
 */
bool admin_linux_account_enabled(const char* uname)
{
    return linux_users.get(uname);
}

/**
 * Add insecure remote (network) basic user.
 *
 * @param uname    Name of the new user.
 * @param password Password of the new user.
 *
 * @return NULL on success or an error string on failure.
 */
const char* admin_add_inet_user(const char* uname, const char* password, enum user_account_type type)
{
    return admin_add_user(&inet_users, INET_USERS_FILE_NAME, uname, password, type);
}

/**
 * Alter network user.
 *
 * @param uname    The user to alter
 * @param password The new password
 *
 * @return NULL on success or an error string on failure.
 */
const char* admin_alter_inet_user(const char* uname, const char* password)
{
    return admin_alter_user(&inet_users, INET_USERS_FILE_NAME, uname, password);
}

/**
 * Remove insecure remote (network) user
 *
 * @param uname    Name of user to be removed.
 * @param password Password of user to be removed.
 *
 * @return NULL on success or an error string on failure.
 */
const char* admin_remove_inet_user(const char* uname)
{
    return admin_remove_user(&inet_users, INET_USERS_FILE_NAME, uname);
}

/**
 * Check for existance of remote user.
 *
 * @param user The user name to test.
 *
 * @return True if the user exists, false otherwise.
 */
bool admin_inet_user_exists(const char* uname)
{
    return inet_users.get(uname);
}

/**
 * Verify a remote user name and password
 *
 * @param username  Username to verify
 * @param password  Password to verify
 *
 * @return True if the username/password combination is valid
 */
bool admin_verify_inet_user(const char* username, const char* password)
{
    bool authenticated = inet_users.authenticate(username, password);

    // If normal authentication didn't work, try PAM.
    // TODO: The reason for 'users_auth' failing is not known here. If the username existed but pw was wrong,
    // should PAM even be attempted?
    if (!authenticated)
    {
        authenticated = admin_user_is_pam_account(username, password);
    }

    return authenticated;
}

bool admin_user_is_inet_admin(const char* username, const char* password)
{
    if (!password)
    {
        password = "";
    }

    bool is_admin = users_is_admin(&inet_users, username, password);
    if (!is_admin)
    {
        is_admin = admin_user_is_pam_account(username, password, USER_ACCOUNT_ADMIN);
    }
    return is_admin;
}

bool admin_user_is_unix_admin(const char* username)
{
    return users_is_admin(&linux_users, username, nullptr);
}

bool admin_have_admin()
{
    return inet_users.admin_count() > 0 || linux_users.admin_count() > 0;
}

bool admin_is_last_admin(const char* user)
{
    return (admin_user_is_inet_admin(user, nullptr) || admin_user_is_unix_admin(user))
           && ((inet_users.admin_count() + linux_users.admin_count()) == 1);
}

bool admin_user_is_pam_account(const std::string& username, const std::string& password,
                               user_account_type min_acc_type)
{
    mxb_assert(min_acc_type == USER_ACCOUNT_BASIC || min_acc_type == USER_ACCOUNT_ADMIN);
    const auto& config = mxs::Config::get();
    auto pam_ro_srv = config.admin_pam_ro_service;
    auto pam_rw_srv = config.admin_pam_rw_service;
    bool have_ro_srv = !pam_ro_srv.empty();
    bool have_rw_srv = !pam_rw_srv.empty();

    if (!have_ro_srv && !have_rw_srv)
    {
        // PAM auth is not configured.
        return false;
    }

    bool auth_attempted = false;
    mxb::PamResult pam_res;
    if (min_acc_type == USER_ACCOUNT_ADMIN)
    {
        // Must be a readwrite user.
        if (have_rw_srv)
        {
            pam_res = mxb::pam_authenticate(username, password, pam_rw_srv);
            auth_attempted = true;
        }
    }
    else
    {
        // Either account type is ok.
        if (have_ro_srv != have_rw_srv)
        {
            // One PAM service is configured.
            auto pam_srv = have_ro_srv ? pam_ro_srv : pam_rw_srv;
            pam_res = mxb::pam_authenticate(username, password, pam_srv);
        }
        else
        {
            // Have both, try ro first.
            pam_res = mxb::pam_authenticate(username, password, pam_ro_srv);
            if (pam_res.type != mxb::PamResult::Result::SUCCESS)
            {
                pam_res = mxb::pam_authenticate(username, password, pam_rw_srv);
            }
        }
        auth_attempted = true;
    }

    if (pam_res.type == mxb::PamResult::Result::SUCCESS)
    {
        return true;
    }
    else if (auth_attempted)
    {
        MXS_LOG_EVENT(maxscale::event::AUTHENTICATION_FAILURE, "%s", pam_res.error.c_str());
    }
    return false;
}
