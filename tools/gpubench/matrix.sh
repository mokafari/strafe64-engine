#!/bin/bash
# matrix2.sh <prefix> <binary>  -- one A/B row per cvar toggle
cd "$(dirname "$0")"; P=$1; export BIN=$2
./run.sh ${P}base
./run.sh ${P}bloom0 +set r_bloom 0
./run.sh ${P}dof0 +set r_dof 0
./run.sh ${P}grade0 +set r_grade 0
./run.sh ${P}fxaa0 +set r_fxaa 0
./run.sh ${P}sun0 +set r_sunShadows 0
./run.sh ${P}sh4096 +set r_shadowMapSize 4096
./run.sh ${P}sh1024 +set r_shadowMapSize 1024
./run.sh ${P}ssao1 +set r_ssao 1
./run.sh ${P}ae0 +set r_autoExposure 0
./run.sh ${P}bodycam1 +set r_bodycam 1
./run.sh ${P}pbr0 +set r_pbr 0 +set r_cubeMapping 0
./run.sh ${P}draw2d0 +set cg_draw2D 0
./run.sh ${P}720 +set r_customwidth 1280 +set r_customheight 720
./run.sh ${P}base2
