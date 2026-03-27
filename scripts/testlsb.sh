#!/bin/sh

(while true; do cat "$2"; done) | pissb -l \
  | sudo sendiq -i /dev/stdin -s 48000 -f "$1" -t float
