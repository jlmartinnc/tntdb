/*
 * Copyright (C) 2007 Tommi Maekitalo
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * As a special exception, you may use this file as part of a free
 * software library without restriction. Specifically, if other files
 * instantiate templates or use macros or inline functions from this
 * file, or you compile this file and link it with other files to
 * produce an executable, this file does not by itself cause the
 * resulting executable to be covered by the GNU General Public
 * License. This exception does not however invalidate any other
 * reasons why the executable file might be covered by the GNU Library
 * General Public License.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <tntdb/oracle/connection.h>
#include <tntdb/oracle/statement.h>
#include <tntdb/oracle/error.h>
#include <tntdb/result.h>
#include <tntdb/statement.h>
#include <cxxtools/log.h>
#include <signal.h>

log_define("tntdb.oracle.connection")

#ifndef HAVE_SIGHANDLER_T
typedef void (*sighandler_t)(int);
#endif

namespace tntdb
{
namespace oracle
{
void Connection::checkError(sword ret, const char* function) const
{
    error::checkError(getErrorHandle(), ret, function);
}

namespace
{
class SighandlerSaver
{
    int signum;
    sighandler_t sighandler;

public:
    explicit SighandlerSaver(int signum_)
        : signum(signum_)
    {
        sighandler = signal(signum, SIG_DFL);
    }

    ~SighandlerSaver()
    {
        signal(signum, sighandler);
    }
};
}

void Connection::logon(const std::string& dblink, const std::string& user, const std::string& password)
{
    log_debug("logon \"" << dblink << "\" user=\"" << user << '"');

    // workaround for OCI problem: OCI redirects SIGINT, which causes Ctrl-C to fail
    SighandlerSaver sighandlerSaver(SIGINT);

    pid = ::getpid();

    sword ret;

    log_finer("create oracle environment");
    ret = OCIEnvCreate(&envhp, OCI_OBJECT|OCI_THREADED|OCI_NO_MUTEX, 0, 0, 0, 0, 0, 0);
    if (ret != OCI_SUCCESS)
        throw Error("cannot create environment handle");
    log_finer("environment handle => " << envhp);

    log_finer("allocate error handle");
    ret = OCIHandleAlloc(envhp, (void**)&errhp, OCI_HTYPE_ERROR, 0, 0);
    if (ret != OCI_SUCCESS)
        throw Error("cannot create error handle");
    log_finer("error handle => " << errhp);

    log_finer("allocate server handle");
    ret = OCIHandleAlloc(envhp, (void**)&srvhp, OCI_HTYPE_SERVER, 0, 0);
    checkError(ret, "OCIHandleAlloc(OCI_HTYPE_SERVER)");
    log_finer("server handle => " << srvhp);

    log_finer("allocate service handle");
    ret = OCIHandleAlloc(envhp, (void**)&svchp, OCI_HTYPE_SVCCTX, 0, 0);
    checkError(ret, "OCIHandleAlloc(OCI_HTYPE_SVCCTX)");
    log_finer("service handle => " << svchp);

    log_finer("attach to server");
    ret = OCIServerAttach(srvhp, errhp, reinterpret_cast<const text*>(dblink.data()), dblink.size(), 0);
    checkError(ret, "OCIServerAttach");

    /* set attribute server context in the service context */
    ret = OCIAttrSet(svchp, OCI_HTYPE_SVCCTX, srvhp, 0, OCI_ATTR_SERVER, errhp);
    checkError(ret, "OCIAttrSet(OCI_ATTR_SERVER)");

    log_finer("allocate session handle");
    ret = OCIHandleAlloc((dvoid*)envhp, (dvoid**)&usrhp, OCI_HTYPE_SESSION, 0, (dvoid**)0);
    checkError(ret, "OCIHandleAlloc(OCI_HTYPE_SESSION)");

    /* set username attribute in user session handle */
    log_finer("set username attribute in session handle");
    ret = OCIAttrSet(usrhp, OCI_HTYPE_SESSION,
      reinterpret_cast<void*>(const_cast<char*>(user.data())),
      user.size(), OCI_ATTR_USERNAME, errhp);
    checkError(ret, "OCIAttrSet(OCI_ATTR_USERNAME)");

    /* set password attribute in user session handle */
    ret = OCIAttrSet(usrhp, OCI_HTYPE_SESSION,
      reinterpret_cast<void*>(const_cast<char*>(password.data())),
      password.size(), OCI_ATTR_PASSWORD, errhp);
    checkError(ret, "OCIAttrSet(OCI_ATTR_PASSWORD)");

    /* start session */
    log_finer("start session");
    ret = OCISessionBegin(svchp, errhp, usrhp, OCI_CRED_RDBMS, OCI_DEFAULT);
    checkError(ret, "OCISessionBegin");

    /* set user session attrubte in the service context handle */
    ret = OCIAttrSet(svchp, OCI_HTYPE_SVCCTX, usrhp, 0, OCI_ATTR_SESSION, errhp);
    checkError(ret, "OCIAttrSet(OCI_ATTR_SESSION)");
}

namespace
{
std::string extractAttribute(std::string& value, const std::string& key)
{
    std::string::size_type p0 = value.find(key);
    if (p0 == std::string::npos)
        return std::string();

    if (p0 > 0 && value.at(p0 - 1) != ';')
        return std::string();

    std::string::size_type p1 = value.find(';', p0);
    if (p1 == std::string::npos)
        p1 = value.size();
    std::string::size_type n = p1 - p0;

    std::string ret(value, p0 + key.size(), n - key.size());
    if (p0 > 0)
        value.erase(p0 - 1, n + 1);
    else
        value.erase(p0, n);

    return ret;
}
}

Connection::Connection(const std::string& url, const std::string& username, const std::string& password)
  : envhp(0),
    srvhp(0),
    errhp(0),
    usrhp(0),
    svchp(0),
    transactionActive(0)
{
    std::string conninfo(url);
    std::string au = extractAttribute(conninfo, "user=");
    std::string ap = extractAttribute(conninfo, "passwd=");
    log_warn_if(!ap.empty(), "password should not be given in dburl");
    log_warn_if(!au.empty() && !username.empty(), "username given in url and parameter");
    log_warn_if(!ap.empty() && !password.empty(), "password given in url and parameter");
    if (au.empty() && ap.empty())
        logon(conninfo, username, password);
    else
        logon(conninfo, au, ap);
}

Connection::~Connection()
{
    if (envhp)
    {
        sword ret;

        try
        {
            log_finer("OCISessionEnd");
            ret = OCISessionEnd(svchp, errhp, usrhp, OCI_DEFAULT);
            checkError(ret, "OCISessionEnd");
        }
        catch (const std::exception& e)
        {
            log_error(e.what());
        }

        try
        {
            log_finer("OCIServerDetach");
            ret = OCIServerDetach(srvhp, errhp, OCI_DEFAULT);
            checkError(ret, "OCIServerDetach");
        }
        catch (const std::exception& e)
        {
            log_error(e.what());
        }

        log_finer("OCIHandleFree(" << envhp << ')');
        ret = OCIHandleFree(envhp, OCI_HTYPE_ENV);
        if (ret == OCI_SUCCESS)
            log_finer("handle released");
        else
            log_error("OCIHandleFree failed");
    }
}

void Connection::beginTransaction()
{
    //log_debug("OCITransStart(" << svchp << ", " << errhp << ')');
    //checkError(OCITransStart(svchp, errhp, 10, OCI_TRANS_NEW), "OCITransStart");
    ++transactionActive;
}

void Connection::commitTransaction()
{
    if (transactionActive == 0 || --transactionActive == 0)
    {
        log_debug("OCITransCommit(" << srvhp << ", " << errhp << ')');
        checkError(OCITransCommit(svchp, errhp, OCI_DEFAULT), "OCITransCommit");
    }
}

void Connection::rollbackTransaction()
{
    if (transactionActive == 0 || --transactionActive == 0)
    {
        log_debug("OCITransRollback(" << srvhp << ", " << errhp << ')');
        checkError(OCITransRollback(svchp, errhp, OCI_DEFAULT), "OCITransRollback");
    }
}

Connection::size_type Connection::execute(const std::string& query)
{
    return prepare(query).execute();
}

tntdb::Result Connection::select(const std::string& query)
{
    return prepare(query).select();
}

Row Connection::selectRow(const std::string& query)
{
    return prepare(query).selectRow();
}

Value Connection::selectValue(const std::string& query)
{
    return prepare(query).selectValue();
}

tntdb::Statement Connection::prepare(const std::string& query)
{
    return tntdb::Statement(std::make_shared<Statement>(*this, query));
}

tntdb::Statement Connection::prepareWithLimit(const std::string& query, const std::string& limit, const std::string& offset)
{
    std::string q;
    if (limit.empty())
    {
        if (offset.empty())
        {
            q = query;
        }
        else
        {
            // no limit, just offset
            q = "select * from (select a.*, rownum tntdbrownum from(";
            q += query;
            q += ")a) where tntdbrownum > :";
            q += offset;
        }
    }
    else if (offset.empty())
    {
        // just limit, no offset
        q = "select * from (select a.*, rownum tntdbrownum from(";
        q += query;
        q += ")a) where tntdbrownum <= :";
        q += limit;
    }
    else
    {
        // limit and offset set
        q = "select * from (select a.*, rownum tntdbrownum from(";
        q += query;
        q += ")a) where tntdbrownum > :";
        q += offset;
        q += " and tntdbrownum <= :";
        q += offset;
        q += " + :";
        q += limit;
    }

    return prepare(q);
}

bool Connection::ping()
{
    try
    {
        if (pid != getpid())
        {
            // database connection is not valid any more after a fork
            log_warn("pid has changed; current pid=" << getpid() << " connection pid=" << pid << ", release environment handle");

            seqStmt.clear();

            sword ret = OCIHandleFree(envhp, OCI_HTYPE_ENV);
            if (ret != OCI_SUCCESS)
                log_error("OCIHandleFree(" << envhp << " OCI_HTYPE_ENV) failed");

            envhp = 0;

            return false;
        }

        // OCIPing crashed on oracle 10.2.0.4.0
#if OCI_MAJOR_VERSION >= 11
        checkError(OCIPing(svchp, errhp, OCI_DEFAULT), "OCIPing");
#else
        char version[64];
        checkError(OCIServerVersion(svchp, errhp, reinterpret_cast<text*>(version), sizeof(version), OCI_HTYPE_SVCCTX));
        log_debug("oracle version " << version);
#endif
        return true;
    }
    catch (const Error&)
    {
        return false;
    }
}

long Connection::lastInsertId(const std::string& name)
{
    tntdb::Statement stmt;
    SeqStmtType::iterator s = seqStmt.find(name);
    if (s == seqStmt.end())
    {
        // check for valid sequence name
        for (std::string::const_iterator it = name.begin(); it != name.end(); ++it)
            if (! ((*it >= 'a' && *it <= 'z')
                || (*it >= 'A' && *it <= 'Z')
                || (*it >= '0' && *it <= '9')
                || *it == '_'))
                throw Error("invalid sequence name \"" + name + '"');

        stmt = prepare(
            "select " + name + ".currval from dual");
        seqStmt[name] = stmt;
    }
    else
        stmt = s->second;

    long ret = 0;
    stmt.selectValue().get(ret);
    return ret;
}

void Connection::lockTable(const std::string& tablename, bool exclusive)
{
    std::string sql = "LOCK TABLE ";
    sql += tablename;
    sql += exclusive ? " IN EXCLUSIVE MODE" : " IN SHARE MODE";
    execute(sql);
}

}
}
