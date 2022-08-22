AC_PREREQ([2.69])
AC_INIT([stm32mp1sign], [1.1], [christian.melki@t2data.com])
AM_INIT_AUTOMAKE([-Wall -Werror foreign])
AC_CONFIG_SRCDIR([stm32mp1sign.c])
AC_CONFIG_HEADERS([config.h])

# Checks for programs.
AC_PROG_CC

# Checks for header files.
AC_CHECK_HEADERS([fcntl.h stdint.h unistd.h])

# Checks for libraries. 
PKG_CHECK_MODULES([CRYPTO], [libcrypto >= 1.1.0])

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_OFF_T
AC_TYPE_SIZE_T
AC_TYPE_UINT32_T
AC_TYPE_UINT8_T

# Checks for library functions.
AC_FUNC_MMAP
AC_CHECK_FUNCS([memset getpass munmap strerror strdup])

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
