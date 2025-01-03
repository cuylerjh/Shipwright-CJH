#ifndef Z_EN_RR_H
#define Z_EN_RR_H

#include <libultraship/libultra.h>
#include "global.h"

struct EnRr;

typedef void (*EnRrActionFunc)(struct EnRr*, PlayState*);

typedef enum {
    /* 0 */ LIKE_LIKE_NORMAL,
    /* 1 */ LIKE_LIKE_SMALL,
    /* 2 */ LIKE_LIKE_GIANT,
    /* 3 */ LIKE_LIKE_INVERT,
    /* 4 */ RUPEE_LIKE,
    /* 5 */ LIFE_LIKE,
} LikeLikeType;

typedef struct {
    /* 0x00 */ f32 height;
    /* 0x04 */ f32 heightTarget;
    /* 0x08 */ Vec3f scale;
    /* 0x14 */ Vec3f scaleTarget;
    /* 0x20 */ Vec3f scaleMod;
    /* 0x2C */ Vec3s rotTarget;
    /* 0x38 */ Vec3s rot;
} EnRrBodySegment; // size = 0x40

typedef struct EnRr {
    /* 0x0000 */ Actor actor;
    /* 0x014C */ EnRrActionFunc actionFunc;
    /* 0x0150 */ ColliderCylinder collider1;
    /* 0x019C */ ColliderCylinder collider2;
    /* 0x01E8 */ s16 frameCount;
    /* 0x01EA */ s16 actionTimer;
    /* 0x01EC */ s16 scrollTimer;
                 s16 scrollMod; // Actively modifies texScroll based on segPhaseVel value.
    /* 0x01EE */ s16 soundTimer;
                 s16 soundEatCount; // Replaces grabTimer. Increments with each sound play during EnRr_GrabPlayer to move through each state and then release.
                 s16 struggleCounter; // Actor-specific counter correlating to player->av2.actionvar2. Determines breakfree while ignoring z_player.c functionality.
                 s16 grabEject; // Increases when hasPlayer but not within a certain xz range. Throws player early.
    /* 0x01F0 */ s16 invincibilityTimer;
    /* 0x01F2 */ s16 effectTimer;
    /* 0x01F4 */ s16 ocTimer;
                 s16 fallTimer; 
                 s16 colPlayerTimer; // MM timer and behavior for nullifying main collider during EnRr_GrabPlayer while still allowing collisions with other objects.
    /* 0x01F6 */ s16 segMovePhase; // Phase angle for wobble and pulsing motion.
                 s16 eatenRupees;
                 s16 dropRupees;
                 s16 stolenLife;
                 s16 bodySegCount;
    /* 0x01F8 */ f32 segPhaseVel;  // Rate at which motion phase changes.
    /* 0x01FC */ f32 segPhaseVelTarget;
    /* 0x0204 */ f32 segWobblePhaseDiffX; // Phase diff between segment X rot. Affects how circular the wobble is.
    /* 0x0208 */ f32 segWobbleXTarget;
    /* 0x020C */ f32 segWobblePhaseDiffZ; // Phase diff between segment Z rot. Affects how circular the wobble is.
    /* 0x0210 */ f32 segWobbleZTarget;
    /* 0x0214 */ f32 pulseSize; // Amplitude of the scale pulsations.
    /* 0x0218 */ f32 pulseSizeTarget;
    /* 0x021C */ f32 wobbleSize; // Amplitude of the wobbling motion.
    /* 0x0220 */ f32 wobbleSizeTarget;
    /* 0x0224 */ EnRrBodySegment bodySegs[5];
    /* 0x0364 */ f32 segMoveRate;
    /* 0x0368 */ f32 shrinkRate;
    /* 0x036C */ f32 swallowOffset;
    /*        */ f32 heightRef1;
    /*        */ f32 heightRef2;
    /*        */ f32 radiusRef;
    /*        */ f32 yShiftRef;
    /*        */ f32 reachHeight;
    /*        */ f32 reachAngle;
    /*        */ f32 rateTimer;
    /*        */ f32 heightRate;
    /*        */ f32 scaleRate1; 
    /*        */ f32 scaleRate2; // Determines scale rate for mouth segment. 
    /*        */ f32 rotXRate;
    /*        */ f32 rotZRate;
                 f32 segPhaseVelRate;
                 f32 segWobbleXRate;
                 f32 segWobbleZRate;
                 f32 pulseSizeRate;
                 f32 wobbleSizeRate;
    /* 0x0370 */ u8 reachState;
                 u8 grabState; // Like reachState, moves through grabStates with unique functionality.
                 u8 reachUp; // Returns true if the player is in a certain range above this actor.
    /* 0x0371 */ u8 isDead;
    /* 0x0372 */ u8 eatenShield;
    /* 0x0373 */ u8 eatenTunic;
                 u8 msgShield; // Moved from function variable to actor variable.
                 u8 msgTunic; // Moved from function variable to actor variable.
                 u8 releaseThrow;
    /* 0x0375 */ u8 retreat;
    /* 0x0376 */ u8 stopScroll;
                 u8 playerIsFacing;
    /* 0x0378 */ s16 hasPlayer;
    /* 0x037C */ Vec3f mouthPos;
    /* 0x0388 */ Vec3f effectPos[5];
} EnRr; 

#endif
