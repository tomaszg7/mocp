[![Build Status](https://travis-ci.org/tomaszg7/mocp.svg?branch=master)](https://travis-ci.org/tomaszg7/mocp)

This is fork of MOC (Music on Console) which tries to follow closely upstream development but includes several experimental features.

# List of major changes in master branch

1. Native Opus decoder using libopusfile
1. Reworked audio formats including support for 32 bit samples, higher configurability of allowed formats and configure time restriction of float computing for machines w/o FPU
1. mp3 decoder using libmpg123
1. Logarithmic audio scaling both in ALSA and in softmixer
1. Better multichannel support, including downmixing 5.1 -> 2.0 and multichannel output from Wavpack and FFmpeg decoders
1. Upgraded sndfile decoder - support for other sample formats and partial BPS reporting
1. Lyrics option changes - AutoloadLyrics does what it would be expected to do - preload lyrics
1. Resampling changes - tri-valued option EnableResample replacing ForceResample
1. Possibility to use "~" in some options in config file
1. Playlist behaviour changes - possibility to automatically enable shuffle for playlist and exporting relative playlists
1. Inotify support

# Branches

* speex - introduces two new resampling methods: one is using speex and the other soxr.
* ratings - includes song rating support pulled from https://github.com/hilgenberg/moc.