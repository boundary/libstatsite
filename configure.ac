AC_INIT([libstatsite], [0.6.1])
AM_INIT_AUTOMAKE
AC_CONFIG_MACRO_DIR([m4])
AC_PROG_CC

AC_PROG_LIBTOOL
AC_PROG_CC_STDC
CFLAGS="$CFLAGS -Wall -Werror -std=c99"

AC_CANONICAL_HOST
case $host_os in
	*linux*) CFLAGS="$CFLAGS -Dlinux -D_BSD_SOURCE -D_DEFAULT_SOURCE -D_POSIX_SOURCE -D_GNU_SOURCE" ;;
	*solaris*) CFLAGS="$CFLAGS -D__EXTENSIONS__" ;;
	*) ;;
esac

LT_INIT

AC_OUTPUT(Makefile include/Makefile include/statsite/Makefile src/Makefile)
