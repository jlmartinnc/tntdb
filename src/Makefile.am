AM_CPPFLAGS = -I$(top_srcdir)/include -I$(top_builddir)/include

lib_LTLIBRARIES = libtntdb.la

libtntdb_la_SOURCES = \
	blob.cpp \
	blobstream.cpp \
	connect.cpp \
	connection.cpp \
	connectionpool.cpp \
	date.cpp \
	datetime.cpp \
	decimal.cpp \
	error.cpp \
	librarymanager.cpp \
	poolconnection.cpp \
	pscconnection.cpp \
	result.cpp \
	resultimpl.cpp \
	row.cpp \
	rowimpl.cpp \
	serialization.cpp \
	sqlbuilder.cpp \
	statement.cpp \
	statement_iterator.cpp \
	stmtparser.cpp \
	time.cpp \
	transaction.cpp \
	valueimpl.cpp

libtntdb_la_LDFLAGS = -version-info @sonumber@ @SHARED_LIB_FLAG@
libtntdb_la_CXXFLAGS = -DDRIVERDIR=\"@driverdir@\" -DABI_CURRENT=\"@abi_current@\"
