#!/bin/bash
# Build slideshow screensaver for SymbOS using scc

SCC="${SCC:-../scc/bin/cc}"

"$SCC" slideshow.c slideshow_msx.s \
    -N "Slideshow" \
    -o slides.sav \
    -h 512

python3 add_preview.py
