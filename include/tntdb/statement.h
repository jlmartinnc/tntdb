/*
 * Copyright (C) 2005 Tommi Maekitalo
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

#ifndef TNTDB_SQL_STATEMENT_H
#define TNTDB_SQL_STATEMENT_H

#include <tntdb/bits/statement.h>
#include <tntdb/bits/statement_iterator.h>
#include <tntdb/bits/rowreader.h>

namespace tntdb
{
/// C++11 style _begin_ function for tntdb::Statement.
inline Statement::const_iterator begin(const Statement& stmt, unsigned fetchsize = 100)
{ return stmt.begin(fetchsize); }

/// C++11 style _end_ function for tntdb::Statement.
inline Statement::const_iterator end(const Statement& stmt)
{ return stmt.end(); }

inline Statement::const_iterator Statement::end() const
{ return const_iterator(); }

template <typename T>
RowReader Statement::const_iterator::get(T& ret) const
{ return RowReader(current).get(ret); }

template <typename T>
RowReader Statement::const_iterator::get(T& ret, bool& nullInd) const
{ return RowReader(current).get(ret, nullInd); }

}

#endif // TNTDB_SQL_STATEMENT_H

