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

#ifndef TNTDB_ORACLE_CONNECTION_H
#define TNTDB_ORACLE_CONNECTION_H

#include <tntdb/iface/iconnection.h>
#include <tntdb/statement.h>
#include <oci.h>
#include <map>
#include <unistd.h>

namespace tntdb
{
/**

 This namespace contains the implementation of the Oracle driver for tntdb.
 Note that the oracle driver is not compiled by default when tntdb is built.
 It must be enabled using the configure switch --with-oracle.

 The driver makes it possible to access a Oracle database using tntdb.

 To get a connection to a Oracle database, the dburl to the tntdb::connect function
 must start with "oracle:". The remaining string specifies the connection parameters
 to the oracle database.

 The attributes user and passwd are extracted and the rest is passed to the
 `OCIServerAttach` function of OCI.

 A typical connection with a Oracle driver looks like that:

 @code
   tntdb::Connection conn = tntdb::connect("oracle:XE;user=hr;passwd=hr");
 @endcode

 */

namespace oracle
{
class Connection : public IConnection
{
    OCIEnv*     envhp;   /* the environment handle */
    OCIServer*  srvhp;   /* the server handle */
    OCIError*   errhp;   /* the error handle */
    OCISession* usrhp;   /* user session handle */
    OCISvcCtx*  svchp;   /* the service handle */
    typedef std::map<std::string, tntdb::Statement> SeqStmtType;
    SeqStmtType seqStmt;
    pid_t       pid;

    void logon(const std::string& dblink, const std::string& user, const std::string& password);

    unsigned transactionActive;

public:
    void checkError(sword ret, const char* function = 0) const;

    Connection(const std::string& url, const std::string& username, const std::string& password);
    ~Connection();

    void beginTransaction();
    void commitTransaction();
    void rollbackTransaction();

    size_type execute(const std::string& query);
    tntdb::Result select(const std::string& query);
    tntdb::Row selectRow(const std::string& query);
    tntdb::Value selectValue(const std::string& query);
    tntdb::Statement prepare(const std::string& query);
    tntdb::Statement prepareWithLimit(const std::string& query, const std::string& limit, const std::string& offset);
    bool ping();
    long lastInsertId(const std::string& name);
    void lockTable(const std::string& tablename, bool exclusive);

    OCIEnv* getEnvHandle() const        { return envhp; }
    OCIError* getErrorHandle() const    { return errhp; }
    OCIServer* getSrvHandle() const     { return srvhp; }
    OCISvcCtx* getSvcCtxHandle() const  { return svchp; }
    bool isTransactionActive() const    { return transactionActive > 0; }
};

}
}

#endif // TNTDB_ORACLE_CONNECTION_H

