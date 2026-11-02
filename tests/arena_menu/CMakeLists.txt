cmake_minimum_required(VERSION 3.25)
project(arena_menu_regression C)
add_executable(arena_menu_regression arena_menu_regression.c ../../engine/code/qcommon/q_shared.c)
target_compile_definitions(arena_menu_regression PRIVATE _CRT_SECURE_NO_WARNINGS)
