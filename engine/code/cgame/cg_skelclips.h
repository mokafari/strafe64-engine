// generated from the tools/modelgen/glb2iqm.py clip list for models/s64/mannequin.iqm
typedef enum {
	SKEL_IDLE_LOOP,
	SKEL_WALK_LOOP,
	SKEL_SPRINT_LOOP,
	SKEL_CROUCH_IDLE_LOOP,
	SKEL_CROUCH_FWD_LOOP,
	SKEL_JUMP_LOOP,
	SKEL_JUMP_START,
	SKEL_JUMP_LAND,
	SKEL_ROLL,
	SKEL_SLIDE_LOOP,
	SKEL_SLIDE_START,
	SKEL_SLIDE_EXIT,
	SKEL_NINJAJUMP_IDLE_LOOP,
	SKEL_NINJAJUMP_START,
	SKEL_NINJAJUMP_LAND,
	SKEL_SWORD_DASH,
	SKEL_SWORD_REGULAR_A,
	SKEL_SWORD_REGULAR_B,
	SKEL_SWORD_REGULAR_C,
	SKEL_SWORD_BLOCK,
	SKEL_CLIMBUP_1M,
	SKEL_NUM
} skelClip_t;

static const struct { int first, count; qboolean loop; } skelClips[SKEL_NUM] = {
	{ 0, 76, qtrue },	// Idle_Loop
	{ 76, 41, qtrue },	// Walk_Loop
	{ 117, 21, qtrue },	// Sprint_Loop
	{ 138, 89, qtrue },	// Crouch_Idle_Loop
	{ 227, 61, qtrue },	// Crouch_Fwd_Loop
	{ 288, 76, qtrue },	// Jump_Loop
	{ 364, 41, qfalse },	// Jump_Start
	{ 405, 39, qfalse },	// Jump_Land
	{ 444, 45, qfalse },	// Roll
	{ 489, 61, qtrue },	// Slide_Loop
	{ 550, 26, qfalse },	// Slide_Start
	{ 576, 16, qfalse },	// Slide_Exit
	{ 592, 61, qtrue },	// NinjaJump_Idle_Loop
	{ 653, 30, qfalse },	// NinjaJump_Start
	{ 683, 39, qfalse },	// NinjaJump_Land
	{ 722, 48, qfalse },	// Sword_Dash
	{ 770, 14, qfalse },	// Sword_Regular_A
	{ 784, 17, qfalse },	// Sword_Regular_B
	{ 801, 61, qfalse },	// Sword_Regular_C
	{ 862, 38, qfalse },	// Sword_Block
	{ 900, 21, qfalse },	// ClimbUp_1m
};
