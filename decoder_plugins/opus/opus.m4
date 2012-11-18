dnl opus

AC_ARG_WITH(opus, AS_HELP_STRING([--without-opus],
                                   [Compile without Ogg Opus support]))

	if test "x$with_opus" != "xno"
	then
		PKG_CHECK_MODULES(OPUS,
			      [opusfile],
			      [AC_SUBST(OPUS_LIBS)
			       AC_SUBST(OPUS_CFLAGS)
			       want_opus="yes"
			       DECODER_PLUGINS="$DECODER_PLUGINS opus"],
			      [true])
	fi

AM_CONDITIONAL([BUILD_opus], [test "$want_opus"])
AC_CONFIG_FILES([decoder_plugins/opus/Makefile])
