dnl opus

AC_ARG_WITH(opus, AS_HELP_STRING([--without-opus],
                                   [Compile without Opus support]))

	if test "x$with_opus" != "xno"
	then
		PKG_CHECK_MODULES(OPUSFILE,
			      [opusfile >= 0.1],
			      [AC_SUBST(OPUSFILE_LIBS)
			       AC_SUBST(OPUSFILE_CFLAGS)
			       want_opus="yes"
			       DECODER_PLUGINS="$DECODER_PLUGINS opus"],
			      [true])
	fi

AM_CONDITIONAL([BUILD_opus], [test "$want_opus"])
AC_CONFIG_FILES([decoder_plugins/opus/Makefile])
