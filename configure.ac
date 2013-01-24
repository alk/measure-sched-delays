AC_INIT([measure-sched-delays], 1.0)
AM_INIT_AUTOMAKE
AC_CONFIG_FILES([Makefile])
AC_PROG_CC

AX_PTHREAD([
        LIBS="$PTHREAD_LIBS $LIBS"
        CFLAGS="$CFLAGS $PTHREAD_CFLAGS"
        CC="$PTHREAD_CC"
], [
        AC_MSG_ERROR([unable to detect pthreads support])
])

AC_OUTPUT
