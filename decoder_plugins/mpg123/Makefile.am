lib_LTLIBRARIES = libmpg123_decoder.la
libdir = $(plugindir)/$(DECODER_PLUGIN_DIR)
libmpg123_decoder_la_LDFLAGS = @PLUGIN_LDFLAGS@
libmpg123_decoder_la_LIBADD = $(MPG123_LIBS)
libmpg123_decoder_la_CFLAGS = $(MPG123_CFLAGS) -I$(top_srcdir)
libmpg123_decoder_la_SOURCES = mpg123.c
