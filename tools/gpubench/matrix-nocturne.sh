#!/bin/bash
# Per-feature GPU cost on the nocturne pool view. total delta vs base = cost of the feature.
cd "$(dirname "$0")"
export MAP=strafe64_nocturne CFG=bench-nocturne.cfg VIEW="0 -1900 300 90 8" OUT=${OUT:-/tmp/s64-gpubench-noct}
./run.sh nbase
./run.sh nwater0 +set r_water 0
./run.sh nmirror0 +set r_water 0 +set r_noportals 1
./run.sh nshafts0 +set r_shafts 0
./run.sh nssao0 +set r_ssao 0
./run.sh nsunrays0 +set r_drawSunRays 0
./run.sh nbloom0 +set r_bloom 0
./run.sh ngrade0 +set r_grade 0
./run.sh nmsaa0 +set r_ext_multisample 0
./run.sh nbase2
