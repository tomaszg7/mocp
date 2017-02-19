#!/bin/sh
pli=`mktemp`
echo "---" > $pli
echo "album: $3" >> $pli
echo "length: $4" >> $pli
echo "title: $2" >> $pli
echo "time: !timestamp" `date "+%F %T"` >> $pli
echo "artist: $1" >>$pli
