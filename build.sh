#!/bin/bash
# Build slideshow screensaver for SymbOS using scc

SCC="${SCC:-../scc/bin/cc}"

"$SCC" slideshow.c \
    -N "Slideshow" \
    -o slideshow.sav \
    -h 512

python3 add_preview.py
