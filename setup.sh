#!/bin/sh

# Takes care of a couple of small setup requirements for a new build.

# Copy the template config
[ -e include/marquee_config.h ] || cp include/marquee_config.h.dist include/marquee_config.h

# Download the assets for the color picker
[ -e data/www/css/huebee.min.css ] || curl --output-dir data/www/css -LO https://unpkg.com/huebee@1/dist/huebee.min.css
[ -e data/www/js/huebee.pkgd.min.js ] || curl --output-dir data/www/js -LO https://unpkg.com/huebee@1/dist/huebee.pkgd.min.js
