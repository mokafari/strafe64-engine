cmake_minimum_required(VERSION 3.25)
project(sound_codec_regression C)
add_executable(codec_regression codec_regression.c ../../engine/code/qcommon/q_shared.c)
target_compile_definitions(codec_regression PRIVATE _CRT_SECURE_NO_WARNINGS)
