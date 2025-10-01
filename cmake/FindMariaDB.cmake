# Try to locate the MariaDB (or MySQL) client development files.
#
# This module defines the following variables when successful:
#   MariaDB_FOUND        - system has the MariaDB client libraries
#   MariaDB_INCLUDE_DIRS - directories containing the headers
#   MariaDB_LIBRARIES    - the libraries needed for linking
#
# The caller can optionally set MariaDB_ROOT to hint at a custom install
# location.

find_path(MariaDB_INCLUDE_DIR
    NAMES mysql.h mariadb/mysql.h
    HINTS ${MariaDB_ROOT}
    PATH_SUFFIXES include mysql mariadb mysql/mysql mariadb/mysql)

find_library(MariaDB_LIBRARY
    NAMES mariadb mariadbclient mysqlclient
    HINTS ${MariaDB_ROOT}
    PATH_SUFFIXES lib lib64 lib/aarch64-linux-gnu lib/arm-linux-gnueabihf lib/arm-linux-gnueabi)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(MariaDB DEFAULT_MSG MariaDB_LIBRARY MariaDB_INCLUDE_DIR)

if (MariaDB_FOUND)
    set(MariaDB_LIBRARIES ${MariaDB_LIBRARY})
    set(MariaDB_INCLUDE_DIRS ${MariaDB_INCLUDE_DIR})
endif()

mark_as_advanced(MariaDB_INCLUDE_DIR MariaDB_LIBRARY)
