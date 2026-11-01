// generated from the tools/modelgen/glb2iqm.py clip list for models/s64/mannequin.iqm
typedef enum {
	SKEL_IDLE_LOOP,
	SKEL_WALK_LOOP,
	SKEL_SPRINT_LOOP,
	SKEL_CROUCH_IDLE_LOOP,
	SKEL_CROUCH_FWD_LOOP,
	SKEL_JUMP_LOOP,
	SKEL_SLIDE_LOOP,
	SKEL_NINJAJUMP_IDLE_LOOP,
	SKEL_SWORD_REGULAR_A,
	SKEL_SWORD_REGULAR_B,
	SKEL_SWORD_REGULAR_C,
	SKEL_SWORD_BLOCK,
	SKEL_NUM
} skelClip_t;

static const struct { int first, count; qboolean loop; } skelClips[SKEL_NUM] = {
	{ 0, 76, qtrue },	// Idle_Loop
	{ 76, 41, qtrue },	// Walk_Loop
	{ 117, 21, qtrue },	// Sprint_Loop
	{ 138, 89, qtrue },	// Crouch_Idle_Loop
	{ 227, 61, qtrue },	// Crouch_Fwd_Loop
	{ 288, 76, qtrue },	// Jump_Loop
	{ 364, 61, qtrue },	// Slide_Loop
	{ 425, 61, qtrue },	// NinjaJump_Idle_Loop
	{ 486, 14, qfalse },	// Sword_Regular_A
	{ 500, 17, qfalse },	// Sword_Regular_B
	{ 517, 61, qfalse },	// Sword_Regular_C
	{ 578, 38, qfalse },	// Sword_Block
};
