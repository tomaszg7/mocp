lib_LTLIBRARIES = libopus_decoder.la
libdir = $(plugindir)/$(DECODER_PLUGIN_DIR)
libopus_decoder_la_LDFLAGS = @PLUGIN_LDFLAGS@
libopus_decoder_la_LIBADD = $(OPUSFILE_LIBS)
libopus_decoder_la_CFLAGS = $(OPUSFILE_CFLAGS) -I$(top_srcdir)
libopus_decoder_la_SOURCES = opus.c
