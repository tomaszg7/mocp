dnl mpg123

AC_ARG_WITH(mpg123, AS_HELP_STRING([--without-mpg123],
                                   [Compile without MPG123 support]))

	if test "x$with_mpg123" != "xno"
	then
		PKG_CHECK_MODULES(MPG123,
			      [libmpg123 >= 1.14],
			      [AC_SUBST(MPG123_LIBS)
			       AC_SUBST(MPG123_CFLAGS)
			       want_mpg123="yes"
			       DECODER_PLUGINS="$DECODER_PLUGINS mpg123"],
			      [true])
 
	fi

AM_CONDITIONAL([BUILD_mpg123], [test "$want_mpg123"])
AC_CONFIG_FILES([decoder_plugins/mpg123/Makefile])
