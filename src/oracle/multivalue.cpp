/*
 * Copyright (C) 2012 Tommi Maekitalo
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

#include <tntdb/oracle/multivalue.h>
#include <tntdb/oracle/statement.h>
#include <tntdb/oracle/number.h>
#include <tntdb/error.h>
#include <cxxtools/log.h>
#include <sstream>
#include <algorithm>

log_define("tntdb.oracle.multivalue")

namespace tntdb
{
namespace oracle
{
namespace
{
    template <typename T>
    T getValue(const std::string& s, const char* tname)
    {
        std::istringstream in(s);
        T ret;
        in >> ret;
        if (!in)
        {
            std::ostringstream msg;
            msg << "can't convert \"" << s << "\" to " << tname;
            throw TypeError(msg.str());
        }
        return ret;
    }

    template <typename T, typename I>
    T getValueFloat(I from, I to, const char* tname)
    {
        // This is a stupid workaround for converting decimal comma into decimal point
        // It should be actually done in OCI
        std::string o;
        o.reserve(to - from);
        std::replace_copy(from, to, std::back_inserter(o), ',', '.');
        return getValue<T>(o, tname);
    }

    template <typename T>
    std::string toString(T v)
    {
        std::ostringstream ret;
        ret << v;
        return ret.str();
    }

    template <>
    std::string toString(float v)
    {
        std::ostringstream ret;
        ret.precision(24);
        ret << v;
        return ret.str();
    }

    template <>
    std::string toString(double v)
    {
        std::ostringstream ret;
        ret.precision(24);
        ret << v;
        return ret.str();
    }

    template <>
    std::string toString(Decimal v)
    {
        std::ostringstream ret;
        ret.precision(24);
        ret << v;
        return ret.str();
    }

    template <typename IntType>
    IntType round(double d)
    {
        if (d > std::numeric_limits<IntType>::max() + .5 || d < std::numeric_limits<IntType>::min() - .5)
        {
            log_warn("overflow when trying to read integer from float " << d);
            throw std::overflow_error("overflow when trying to read integer from float");
        }

        return static_cast<IntType>(d >= 0 ? d + .5 : d - .5);
    }

}

MultiValue::MultiValue(Statement& stmt, ub4 pos, unsigned n)
  : _defp(0),
    _conn(stmt.getConnection()),
    _type(0),
    _n(n),
    _data(0)
{
    sword ret;

    /* get parameter-info */
    OCIParam* paramp = 0;
    log_finer("OCIParamGet(" << static_cast<void*>(stmt.getHandle()) << ')');
    ret = OCIParamGet(stmt.getHandle(), OCI_HTYPE_STMT,
        _conn.getErrorHandle(),
        reinterpret_cast<void**>(&paramp), pos + 1);
    _conn.checkError(ret, "OCIParamGet");

    init(stmt, paramp, pos);

    OCIDescriptorFree(paramp, OCI_DTYPE_PARAM);
}

MultiValue::MultiValue(Statement& stmt, OCIParam* paramp, ub4 pos, unsigned n)
  : _defp(0),
    _conn(stmt.getConnection()),
    _type(0),
    _n(n),
    _data(0)
{
    init(stmt, paramp, pos);
}

void MultiValue::init(Statement& stmt, OCIParam* paramp, ub4 pos)
{
    sword ret;

    _len.resize(_n);
    _nullind.resize(_n);

    /* retrieve column name */
    ub4 col_name_len = 0;
    text* col_name = 0;
    ret = OCIAttrGet(paramp, OCI_DTYPE_PARAM, &col_name, &col_name_len,
        OCI_ATTR_NAME, _conn.getErrorHandle());

    _conn.checkError(ret, "OCIAttrGet(OCI_ATTR_NAME)");
    _colName.assign(reinterpret_cast<const char*>(col_name), col_name_len);

    /* retrieve the data type attribute */
    ret = OCIAttrGet(paramp, OCI_DTYPE_PARAM, &_type, 0, OCI_ATTR_DATA_TYPE,
        _conn.getErrorHandle());
    _conn.checkError(ret, "OCIAttrGet(OCI_ATTR_DATA_TYPE)");

    ret = OCIAttrGet(paramp, OCI_DTYPE_PARAM, &_collen, 0, OCI_ATTR_DATA_SIZE,
        _conn.getErrorHandle());
    _conn.checkError(ret, "OCIAttrGet(OCI_ATTR_DATA_SIZE)");

    log_finer("column name=\"" << _colName << "\" type=" << _type << " size=" << _collen);

    /* define outputvariable */
    switch (_type)
    {
        case SQLT_DAT:
        case SQLT_TIMESTAMP:
        case SQLT_TIMESTAMP_TZ:
        case SQLT_TIMESTAMP_LTZ:
            {
                log_finer("OCIDefineByPos(SQLT_TIMESTAMP)");

                _data = new char[_n * sizeof(OCIDateTime*)];
                OCIDateTime** p = reinterpret_cast<OCIDateTime**>(_data);
                log_finer("OCIDescriptorAlloc(OCI_DTYPE_TIMESTAMP) (" << _n << " times)");
                for (unsigned n = 0; n < _n; ++n)
                {
                    ret = OCIDescriptorAlloc(_conn.getEnvHandle(),
                        reinterpret_cast<void**>(&p[n]), OCI_DTYPE_TIMESTAMP, 0, 0);
                    _conn.checkError(ret, "OCIDescriptorAlloc(OCI_DTYPE_TIMESTAMP)");
                }

                ret = OCIDefineByPos(stmt.getHandle(), &_defp,
                    _conn.getErrorHandle(), pos + 1, _data,
                    sizeof(OCIDateTime*), SQLT_TIMESTAMP, _nullind.data(), _len.data(), 0, OCI_DEFAULT);
            }
            break;

        case SQLT_INT:
            log_finer("OCIDefineByPos(SQLT_INT)");
            _data = new char[_n * sizeof(long)];
            ret = OCIDefineByPos(stmt.getHandle(), &_defp,
                _conn.getErrorHandle(), pos + 1, _data,
                sizeof(long), SQLT_INT, _nullind.data(), _len.data(), 0, OCI_DEFAULT);
            break;

        case SQLT_UIN:
            log_finer("OCIDefineByPos(SQLT_UIN)");
            _data = new char[_n * sizeof(unsigned long)];
            ret = OCIDefineByPos(stmt.getHandle(), &_defp,
                _conn.getErrorHandle(), pos + 1, _data,
                sizeof(unsigned long), SQLT_UIN, _nullind.data(), _len.data(), 0, OCI_DEFAULT);
            break;

        case SQLT_FLT:
            log_finer("OCIDefineByPos(SQLT_FLT)");
            _data = new char[_n * sizeof(double)];
            ret = OCIDefineByPos(stmt.getHandle(), &_defp,
                _conn.getErrorHandle(), pos + 1, _data,
                sizeof(double), SQLT_FLT, _nullind.data(), _len.data(), 0, OCI_DEFAULT);
            break;

        case SQLT_NUM:
        case SQLT_VNU:
            log_finer("OCIDefineByPos(SQLT_VNU)");
            _data = new char[_n * OCI_NUMBER_SIZE];
            std::fill(_data, _data + _n * OCI_NUMBER_SIZE, 0);
            ret = OCIDefineByPos(stmt.getHandle(), &_defp,
                _conn.getErrorHandle(), pos + 1, _data,
                OCI_NUMBER_SIZE, SQLT_VNU, _nullind.data(), _len.data(), 0, OCI_DEFAULT);
            break;

        case SQLT_BLOB:
            {
                log_finer("OCIDefineByPos(SQLT_LOB)");

                _data = new char[_n * sizeof(OCILobLocator*)];

                OCIDateTime** p = reinterpret_cast<OCIDateTime**>(_data);
                log_finer("OCIDescriptorAlloc(OCI_DTYPE_LOB) (" << _n << " times)");
                for (unsigned n = 0; n < _n; ++n)
                {
                    ret = OCIDescriptorAlloc(_conn.getEnvHandle(),
                        reinterpret_cast<void**>(&p[n]), OCI_DTYPE_LOB, 0, 0);
                    _conn.checkError(ret, "OCIDescriptorAlloc(OCI_DTYPE_LOB)");
                }

                ret = OCIDefineByPos(stmt.getHandle(), &_defp,
                    _conn.getErrorHandle(), pos + 1, _data,
                    sizeof(OCILobLocator*), SQLT_BLOB, _nullind.data(), _len.data(), 0, OCI_DEFAULT);
            }
            break;

        case SQLT_BIN:
            log_finer("OCIDefineByPos(SQLT_BIN)");
            _data = new char[(_collen + 16) * _n];
            ret = OCIDefineByPos(stmt.getHandle(), &_defp,
                _conn.getErrorHandle(), pos + 1, _data,
                (_collen + 16), SQLT_BIN, _nullind.data(), _len.data(), 0, OCI_DEFAULT);
            break;

        default:
            log_finer("OCIDefineByPos(SQLT_AFC)");
            _data = new char[(_collen + 16) * _n];
            ret = OCIDefineByPos(stmt.getHandle(), &_defp,
                _conn.getErrorHandle(), pos + 1, _data,
                (_collen + 16), SQLT_AFC, _nullind.data(), _len.data(), 0, OCI_DEFAULT);
            break;
    }

    _conn.checkError(ret, "OCIDefineByPos");
}

MultiValue::~MultiValue()
{
    if (_defp)
        OCIHandleFree(_defp, OCI_HTYPE_DEFINE);

    if (_data)
    {
        switch(_type)
        {
            case SQLT_DAT:
            case SQLT_TIMESTAMP:
            case SQLT_TIMESTAMP_TZ:
            case SQLT_TIMESTAMP_LTZ:
                {
                    OCIDateTime** p = reinterpret_cast<OCIDateTime**>(_data);
                    log_finer("OCIDescriptorFree(desc, OCI_DTYPE_TIMESTAMP) (" << _n << " times)");
                    for (unsigned n = 0; n < _n; ++n)
                        OCIDescriptorFree(p[n], OCI_DTYPE_TIMESTAMP);
                }
                break;

            case SQLT_BLOB:
                {
                    OCILobLocator** p = reinterpret_cast<OCILobLocator**>(_data);
                    log_finer("OCIDescriptorFree(desc, OCI_DTYPE_LOB) (" << _n << " times)");
                    for (unsigned n = 0; n < _n; ++n)
                        OCIDescriptorFree(p[n], OCI_DTYPE_LOB);
                }
                break;

            default:
                break;
        }

        delete[] _data;
    }
}

bool MultiValue::isNull(unsigned n) const
{
    log_trace("isNull(" << n << ')');

    return _nullind[n] != 0;
}

bool MultiValue::getBool(unsigned n) const
{
    log_trace("getBool(" << n << "), type=" << _type);

    if (_nullind[n] != 0)
        throw NullValue();

    switch (_type)
    {
        case SQLT_DAT:
        case SQLT_TIMESTAMP:
        case SQLT_TIMESTAMP_TZ:
        case SQLT_TIMESTAMP_LTZ:
            throw TypeError();

        case SQLT_INT:
            return longValue(n) != 0;

        case SQLT_UIN:
            return unsignedValue(n) != 0;

        case SQLT_FLT:
            return doubleValue(n) != 0;

        case SQLT_NUM:
        case SQLT_VNU:
        {
            int value;
            log_finer("OCINumberToInt");
            sword ret = OCINumberToInt(_conn.getErrorHandle(), &number(n), sizeof(int), OCI_NUMBER_SIGNED, &value);
            _conn.checkError(ret, "OCINumberToInt");
            return value != 0;
        }

        default:
            return data(n)[0] == 't' || data(n)[0] == 'T'
              || data(n)[0] == 'y' || data(n)[0] == 'Y'
              || data(n)[0] == '1';

    }
}

short MultiValue::getShort(unsigned n) const
{
    log_trace("getShort(" << n << "), type=" << _type);

    if (_nullind[n] != 0)
        throw NullValue();

    switch (_type)
    {
        case SQLT_DAT:
        case SQLT_TIMESTAMP:
        case SQLT_TIMESTAMP_TZ:
        case SQLT_TIMESTAMP_LTZ:
            throw TypeError();

        case SQLT_INT:
        case SQLT_UIN:
            return static_cast<short>(longValue(n));

        case SQLT_FLT:
            return round<short>(doubleValue(n));

        case SQLT_NUM:
        case SQLT_VNU:
        {
            short value;
            log_finer("OCINumberToInt");
            sword ret = OCINumberToInt(_conn.getErrorHandle(), &number(n), sizeof(short), OCI_NUMBER_SIGNED, &value);
            _conn.checkError(ret, "OCINumberToInt");
            return value;
        }

        default:
            return getValue<short>(std::string(data(n), _len[n]), "short");
    }
}

int MultiValue::getInt(unsigned n) const
{
    log_trace("getInt(" << n << "), type=" << _type);

    if (_nullind[n] != 0)
        throw NullValue();

    switch (_type)
    {
        case SQLT_DAT:
        case SQLT_TIMESTAMP:
        case SQLT_TIMESTAMP_TZ:
        case SQLT_TIMESTAMP_LTZ:
            throw TypeError();

        case SQLT_INT:
        case SQLT_UIN:
            return static_cast<int>(longValue(n));

        case SQLT_FLT:
            return round<int>(doubleValue(n));

        case SQLT_NUM:
        case SQLT_VNU:
        {
            int value;
            log_finer("OCINumberToInt");
            sword ret = OCINumberToInt(_conn.getErrorHandle(), &number(n), sizeof(int), OCI_NUMBER_SIGNED, &value);
            _conn.checkError(ret, "OCINumberToInt");
            return value;
        }

        default:
            return getValue<int>(std::string(data(n), _len[n]), "int");
    }
}

long MultiValue::getLong(unsigned n) const
{
    log_trace("getLong(" << n << "), type=" << _type);

    if (_nullind[n] != 0)
        throw NullValue();

    switch (_type)
    {
        case SQLT_DAT:
        case SQLT_TIMESTAMP:
        case SQLT_TIMESTAMP_TZ:
        case SQLT_TIMESTAMP_LTZ:
            throw TypeError();

        case SQLT_INT:
        case SQLT_UIN:
            return static_cast<long>(longValue(n));

        case SQLT_FLT:
            return round<long>(doubleValue(n));

        case SQLT_NUM:
        case SQLT_VNU:
        {
            long value;
            log_finer("OCINumberToInt");
            sword ret = OCINumberToInt(_conn.getErrorHandle(), &number(n), sizeof(long), OCI_NUMBER_SIGNED, &value);
            _conn.checkError(ret, "OCINumberToInt");
            return value;
        }

        default:
            return getValue<long>(std::string(data(n), _len[n]), "long");
    }
}

int32_t MultiValue::getInt32(unsigned n) const
{
    log_trace("getInt32(" << n << "), type=" << _type);

    return getInt(n);
}

unsigned short MultiValue::getUnsignedShort(unsigned n) const
{
    log_finer("getUnsignedShort(" << n << "), type=" << _type);

    if (_nullind[n] != 0)
        throw NullValue();

    switch (_type)
    {
        case SQLT_DAT:
        case SQLT_TIMESTAMP:
        case SQLT_TIMESTAMP_TZ:
        case SQLT_TIMESTAMP_LTZ:
            throw TypeError();

        case SQLT_INT:
        case SQLT_UIN:
            return static_cast<unsigned short>(longValue(n));

        case SQLT_FLT:
            return round<unsigned short>(doubleValue(n));

        case SQLT_NUM:
        case SQLT_VNU:
        {
            unsigned short value;
            log_finer("OCINumberToInt");
            sword ret = OCINumberToInt(_conn.getErrorHandle(), &number(n), sizeof(unsigned short), OCI_NUMBER_UNSIGNED, &value);
            _conn.checkError(ret, "OCINumberToInt");
            return value;
        }

        default:
            return getValue<unsigned short>(std::string(data(n), _len[n]), "unsigned short");
    }
}

unsigned MultiValue::getUnsigned(unsigned n) const
{
    log_trace("getUnsigned(" << n << "), type=" << _type);

    if (_nullind[n] != 0)
        throw NullValue();

    switch (_type)
    {
        case SQLT_DAT:
        case SQLT_TIMESTAMP:
        case SQLT_TIMESTAMP_TZ:
        case SQLT_TIMESTAMP_LTZ:
            throw TypeError();

        case SQLT_INT:
        case SQLT_UIN:
            return static_cast<unsigned>(longValue(n));

        case SQLT_FLT:
            return round<unsigned>(doubleValue(n));

        case SQLT_NUM:
        case SQLT_VNU:
        {
            unsigned value;
            log_finer("OCINumberToInt");
            sword ret = OCINumberToInt(_conn.getErrorHandle(), &number(n), sizeof(unsigned), OCI_NUMBER_UNSIGNED, &value);
            _conn.checkError(ret, "OCINumberToInt");
            return value;
        }

        default:
            return getValue<unsigned>(std::string(data(n), _len[n]), "long");
    }
}

unsigned long MultiValue::getUnsignedLong(unsigned n) const
{
    log_trace("getUnsignedLong(" << n << "), type=" << _type);

    if (_nullind[n] != 0)
        throw NullValue();

    switch (_type)
    {
        case SQLT_DAT:
        case SQLT_TIMESTAMP:
        case SQLT_TIMESTAMP_TZ:
        case SQLT_TIMESTAMP_LTZ:
            throw TypeError();

        case SQLT_INT:
        case SQLT_UIN:
            return static_cast<unsigned long>(longValue(n));

        case SQLT_FLT:
            return round<unsigned long>(doubleValue(n));

        case SQLT_NUM:
        case SQLT_VNU:
        {
            unsigned long value;
            log_finer("OCINumberToInt");
            sword ret = OCINumberToInt(_conn.getErrorHandle(), &number(n), sizeof(unsigned long), OCI_NUMBER_UNSIGNED, &value);
            _conn.checkError(ret, "OCINumberToInt");
            return value;
        }

        default:
            return getValue<unsigned long>(std::string(data(n), _len[n]), "unsigned long");
    }
}

uint32_t MultiValue::getUnsigned32(unsigned n) const
{
    log_trace("getUnsigned32(" << n << "), type=" << _type);

    return getUnsigned(n);
}

int64_t MultiValue::getInt64(unsigned n) const
{
    log_trace("getInt64(" << n << "), type=" << _type);

    if (_nullind[n] != 0)
        throw NullValue();

    switch (_type)
    {
        case SQLT_DAT:
        case SQLT_TIMESTAMP:
        case SQLT_TIMESTAMP_TZ:
        case SQLT_TIMESTAMP_LTZ:
            throw TypeError();

        case SQLT_INT:
        case SQLT_UIN:
            return static_cast<int64_t>(longValue(n));

        case SQLT_FLT:
            return round<int64_t>(doubleValue(n));

        case SQLT_NUM:
        case SQLT_VNU:
        {
            int64_t value;
            log_finer("OCINumberToInt");
            sword ret = OCINumberToInt(_conn.getErrorHandle(), &number(n), sizeof(int64_t), OCI_NUMBER_SIGNED, &value);
            _conn.checkError(ret, "OCINumberToInt");
            return value;
        }

        default:
            return getValue<int64_t>(std::string(data(n), _len[n]), "int64_t");
    }
}

uint64_t MultiValue::getUnsigned64(unsigned n) const
{
    log_trace("getUnsigned64(" << n << "), type=" << _type);

    if (_nullind[n] != 0)
        throw NullValue();

    switch (_type)
    {
        case SQLT_DAT:
        case SQLT_TIMESTAMP:
        case SQLT_TIMESTAMP_TZ:
        case SQLT_TIMESTAMP_LTZ:
            throw TypeError();

        case SQLT_INT:
        case SQLT_UIN:
            return static_cast<uint64_t>(longValue(n));

        case SQLT_FLT:
            return round<uint64_t>(doubleValue(n));

        case SQLT_NUM:
        case SQLT_VNU:
        {
            uint64_t value;
            log_finer("OCINumberToInt");
            sword ret = OCINumberToInt(_conn.getErrorHandle(), &number(n), sizeof(uint64_t), OCI_NUMBER_UNSIGNED, &value);
            _conn.checkError(ret, "OCINumberToInt");
            return value;
        }

        default:
            return getValue<uint64_t>(std::string(data(n), _len[n]), "uint64_t");
    }
}

Decimal MultiValue::getDecimal(unsigned n) const
{
    log_trace("getDecimal(" << n << "), type=" << _type);

    if (_nullind[n] != 0)
        throw NullValue();

    switch (_type)
    {
        case SQLT_DAT:
        case SQLT_TIMESTAMP:
        case SQLT_TIMESTAMP_TZ:
        case SQLT_TIMESTAMP_LTZ:
            throw TypeError();

        case SQLT_INT:
        case SQLT_UIN:
            return Decimal(longValue(n), 0);

        case SQLT_FLT:
            return Decimal(doubleValue(n));

        case SQLT_NUM:
        case SQLT_VNU:
            return decimal(n);

        default:
            return getValue<Decimal>(std::string(data(n), _len[n]), "Decimal");
    }
}

float MultiValue::getFloat(unsigned n) const
{
    log_trace("getFloat(" << n << "), type=" << _type);

    if (_nullind[n] != 0)
        throw NullValue();

    switch (_type)
    {
        case SQLT_DAT:
        case SQLT_TIMESTAMP:
        case SQLT_TIMESTAMP_TZ:
        case SQLT_TIMESTAMP_LTZ:
            throw TypeError();

        case SQLT_INT:
        case SQLT_UIN:
            return static_cast<float>(longValue(n));

        case SQLT_FLT:
            return round<float>(doubleValue(n));

        case SQLT_NUM:
        case SQLT_VNU:
        {
            float value;
            log_finer("OCINumberToReal");
            sword ret = OCINumberToReal(_conn.getErrorHandle(), &number(n), sizeof(float), &value);
            _conn.checkError(ret, "OCINumberToReal");
            return value;
        }

        default:
            return getValueFloat<float>(data(n), data(n) + _len[n], "float");
    }
}

double MultiValue::getDouble(unsigned n) const
{
    log_trace("getDouble(" << n << "), type=" << _type);

    if (_nullind[n] != 0)
        throw NullValue();

    switch (_type)
    {
        case SQLT_DAT:
        case SQLT_TIMESTAMP:
        case SQLT_TIMESTAMP_TZ:
        case SQLT_TIMESTAMP_LTZ:
            throw TypeError();

        case SQLT_INT:
        case SQLT_UIN:
            return static_cast<double>(longValue(n));

        case SQLT_FLT:
            return static_cast<double>(doubleValue(n));

        case SQLT_NUM:
        case SQLT_VNU:
        {
            double value;
            log_finer("OCINumberToReal");
            sword ret = OCINumberToReal(_conn.getErrorHandle(), &number(n), sizeof(double), &value);
            _conn.checkError(ret, "OCINumberToReal");
            return value;
        }

        default:
            return getValueFloat<double>(data(n), data(n) + _len[n], "double");
    }
}

char MultiValue::getChar(unsigned n) const
{
    log_trace("getChar(" << n << "), type=" << _type);

    if (_nullind[n] != 0)
        throw NullValue();

    switch (_type)
    {
        case SQLT_DAT:
        case SQLT_TIMESTAMP:
        case SQLT_TIMESTAMP_TZ:
        case SQLT_TIMESTAMP_LTZ:
        case SQLT_INT:
        case SQLT_UIN:
        case SQLT_FLT:
        case SQLT_NUM:
        case SQLT_VNU:
            throw TypeError();

        default:
            return data(n)[0];
    }
}

void MultiValue::getString(unsigned n, std::string& ret) const
{
    log_trace("getString(" << n << "), type=" << _type);

    if (_type != SQLT_AFC && _type != SQLT_CHR && _nullind[n] != 0)
        throw NullValue();

    switch (_type)
    {
        case SQLT_DAT:
        case SQLT_TIMESTAMP:
        case SQLT_TIMESTAMP_TZ:
        case SQLT_TIMESTAMP_LTZ:
            ret = getDatetime(n).getIso();
            break;

        case SQLT_INT:
        case SQLT_UIN:
            ret = toString(longValue(n));
            break;

        case SQLT_FLT:
            ret = toString(doubleValue(n));
            break;

        case SQLT_NUM:
        case SQLT_VNU:
            ret = decimal(n).toString();
            break;

        default:
            ret.assign(&data(n)[0], _len[n] > 0 ? _len[n] : 0);
    }
}

void MultiValue::getBlob(unsigned n, tntdb::Blob& ret) const
{
    log_trace("getBlob(" << n << "), type=" << _type);

    if (_nullind[n] != 0)
        throw NullValue();

    switch (_type)
    {
        case SQLT_DAT:
        case SQLT_TIMESTAMP:
        case SQLT_TIMESTAMP_TZ:
        case SQLT_TIMESTAMP_LTZ:
        case SQLT_INT:
        case SQLT_UIN:
        case SQLT_FLT:
        case SQLT_NUM:
        case SQLT_VNU:
            throw TypeError();

        case SQLT_BLOB:
            Blob(_conn, blob(n)).getData(ret);
            break;

        default:
            ret.assign(data(n), _len[n] > 0 ? _len[n] - 1 : 0);
    }
}

Date MultiValue::getDate(unsigned n) const
{
    log_trace("getDate(" << n << "), type=" << _type);

    if (_nullind[n] != 0)
        throw NullValue();

    switch (_type)
    {
        case SQLT_DAT:
        case SQLT_TIMESTAMP:
        case SQLT_TIMESTAMP_TZ:
        case SQLT_TIMESTAMP_LTZ:
            return Datetime(&_conn, datetime(n)).getDate();

        default:
            throw TypeError();
    }
}

Time MultiValue::getTime(unsigned n) const
{
    log_trace("getTime(" << n << "), type=" << _type);

    if (_nullind[n] != 0)
        throw NullValue();

    switch (_type)
    {
        case SQLT_DAT:
        case SQLT_TIMESTAMP:
        case SQLT_TIMESTAMP_TZ:
        case SQLT_TIMESTAMP_LTZ:
            return Datetime(&_conn, datetime(n)).getTime();

        default:
            throw TypeError();
    }
}

tntdb::Datetime MultiValue::getDatetime(unsigned n) const
{
    log_trace("Datetime (" << n << "), type=" << _type);

    if (_nullind[n] != 0)
        throw NullValue();

    switch (_type)
    {
        case SQLT_DAT:
        case SQLT_TIMESTAMP:
        case SQLT_TIMESTAMP_TZ:
        case SQLT_TIMESTAMP_LTZ:
            return Datetime(&_conn, datetime(n)).getDatetime();

        default:
            throw TypeError();
    }
}

}
}
