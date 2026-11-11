add_executable(movement_regression movement_regression.c
 ../../engine/code/game/bg_pmove.c ../../engine/code/game/bg_slidemove.c
 ../../engine/code/game/bg_misc.c ../../engine/code/qcommon/q_math.c
 ../../engine/code/qcommon/q_shared.c)
target_compile_definitions(movement_regression PRIVATE _CRT_SECURE_NO_WARNINGS)
if(NOT MSVC)
 target_link_libraries(movement_regression PRIVATE m)
endif()
foreach(case IN ITEMS acceleration braking diagonal jump-held air-jump-limit landing crouch-clearance wall-slide corner coyote slide terminal-speed slope coyote-expired coyote-head-bump coyote-held coyote-single-use bunnyhop wall-jump wall-jump-held wall-run-detach wall-jump-limit step-low step-high vault vault-ceiling coyote-air-spawn coyote-rearm ledge-walkoff ledge-idle vault-ceiling-path water-levels water-swim water-sink water-exit wall-run-expiry wall-run-reentry wall-run-state)
 foreach(scale IN ITEMS normal slow)
  add_test(NAME movement.${case}.${scale} COMMAND movement_regression ${case} ${scale})
 endforeach()
endforeach()
foreach(case IN ITEMS water-jump water-jump-blocked water-jump-fall wall-catch wall-catch-kick wall-catch-expire wall-catch-release wall-catch-approach wall-catch-no-input)
 foreach(scale IN ITEMS normal slow)
  add_test(NAME movement.${case}.${scale} COMMAND movement_regression ${case} ${scale})
 endforeach()
endforeach()

foreach(case IN ITEMS route-jump route-wallkick route-held route-timeout route-death route-no-wall route-cover)
 foreach(scale IN ITEMS normal slow)
  add_test(NAME movement.${case}.${scale} COMMAND movement_regression ${case} ${scale})
 endforeach()
endforeach()
