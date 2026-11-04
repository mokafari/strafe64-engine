add_executable(slowmo_regression slowmo_regression.c
  ../../engine/code/game/bg_pmove.c ../../engine/code/game/bg_slidemove.c
  ../../engine/code/game/bg_misc.c ../../engine/code/qcommon/q_math.c
  ../../engine/code/qcommon/q_shared.c)
target_compile_definitions(slowmo_regression PRIVATE _CRT_SECURE_NO_WARNINGS)
if(NOT MSVC)
  target_link_libraries(slowmo_regression PRIVATE m)
endif()
foreach(case IN ITEMS integration cadence grounded frame-rates ownership overload movement gravity timers angles reset)
  add_test(NAME slowmo.${case} COMMAND slowmo_regression ${case})
endforeach()
