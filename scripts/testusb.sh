#!/bin/sh

(while true; do cat "$2"; done) | ssb_stream -u \
  | sudo sendiq -i /dev/stdin -s 48000 -f "$1" -t float

