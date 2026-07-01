"""strafegen_physics — movement-mod tuning mirror (code/game/bg_pmove.c).

Derived jump geometry the generators size gaps/ledges against. Pure constants +
jump_range(); no other strafegen imports.
"""
# Movement-mod tuning. These mirror code/game/bg_pmove.c + bg_local.h —
# if the mod's numbers change, change them here so courses stay solvable.
# ======================================================================
GRAVITY       = 1000.0   # g_gravity default (snappier, less floaty)
RUN_SPEED     = 320.0    # g_speed default
JUMP_VELOCITY = 300.0    # bg_local.h JUMP_VELOCITY (raised with gravity, apex ~same)
STEPSIZE      = 18.0     # bg_local.h STEPSIZE
DJ_BOOST      = 75.0     # pm_doubleJumpBoost
SLIDE_MIN     = 250.0    # pm_slideMinSpeed
SLIDE_JUMP    = 1.08     # pm_slideJumpBoost
BHOP_MAX      = 1.10     # pm_bhopBoostMax
WALLJUMP_KICK = 200.0    # pm_wallJumpKick
WALLJUMP_VZ   = 250.0    # pm_wallJumpVelocity
WALLJUMP_MAX  = 2        # pm_wallJumpMax

AIR_TIME  = 2.0 * JUMP_VELOCITY / GRAVITY                    # 0.675 s
JUMP_APEX = JUMP_VELOCITY ** 2 / (2.0 * GRAVITY)             # 45.6 u
DJ_APEX   = (JUMP_VELOCITY + DJ_BOOST) ** 2 / (2 * GRAVITY)  # 74.4 u
LEDGE_SJ  = JUMP_APEX + STEPSIZE                             # ~63.6 u
LEDGE_DJ  = DJ_APEX + STEPSIZE                               # ~92.4 u


def jump_range(speed):
    """Flat-ground gap crossable at a given horizontal speed."""
    return speed * AIR_TIME


# safety margin applied to every reachability bound
SAFETY = 0.80
