/*
 * File: z_en_rr.c
 * Overlay: ovl_En_Rr
 * Description: Like Like
 */

#include "z_en_rr.h"
#include "objects/object_rr/object_rr.h"
#include "vt.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
#include <assert.h>
#include "soh/Enhancements/custom-message/CustomMessageTypes.h"

#define FLAGS                                                                                 \
    (ACTOR_FLAG_ATTENTION_ENABLED | ACTOR_FLAG_HOSTILE | ACTOR_FLAG_UPDATE_CULLING_DISABLED | \
     ACTOR_FLAG_DRAW_CULLING_DISABLED | ACTOR_FLAG_HOOKSHOT_PULLS_PLAYER)

#define THIS ((EnRr*)thisx)

// Helps declutter conditions and group overlapping types together.
#define TYPE_STEAL(this) ((this)->actor.params >= 0 && (this)->actor.params <= 5)
#define TYPE_STATIONARY(this) \
    ((this)->actor.params == LIKE_LIKE_STATIONARY || (this)->actor.params == LIKE_LIKE_STATIONARY_INVERT)
#define TYPE_INVERT(this) \
    ((this)->actor.params == LIKE_LIKE_INVERT || (this)->actor.params == LIKE_LIKE_STATIONARY_INVERT)
#define TYPE_DRAIN(this) ((this)->actor.params >= RUPEE_LIKE && (this)->actor.params <= MAGIC_LIKE)

// Scales all other factors relative to OoT Like Like's size
#define SCALE_XZ_MULT (0.015f / 0.013f)

#define SEG_PHASE_VEL_DEFAULT 2621
#define WOBBLE_SIZE_DEFAULT 2560.0f
#define PULSE_SIZE_DEFAULT 0.15f
#define WOBBLE_DIFF_X_DEFAULT 3.0f
#define WOBBLE_DIFF_Z_DEFAULT 1.0f
#define SCALE_MOD_Y_DEFAULT 0.0675f

#define BREAKFREE_TARGET 100

#define BASE_SEG_HEIGHT 500.0f

typedef enum {
    /* 0x0 */ RR_DMG_NONE,
    /* 0x1 */ RR_DMG_STUN,
    /* 0x2 */ RR_DMG_FIRE,
    /* 0x3 */ RR_DMG_ICE,
    /* 0x4 */ RR_DMG_LIGHT_MAGIC,
    /* 0x5 */ RR_DMG_HAMMER,
    /* 0xB */ RR_DMG_LIGHT_ARROW = 11,
    /* 0xC */ RR_DMG_SHDW_ARROW,
    /* 0xD */ RR_DMG_WIND_ARROW,
    /* 0xE */ RR_DMG_SPRT_ARROW,
    /* 0xF */ RR_DMG_NORMAL
} EnRrDamageEffect;

void EnRr_Init(Actor* thisx, PlayState* play);
void EnRr_Destroy(Actor* thisx, PlayState* play);
void EnRr_Update(Actor* thisx, PlayState* play);
void EnRr_Draw(Actor* thisx, PlayState* play2);

void EnRr_Approach(EnRr* this, PlayState* play);
void EnRr_Reach(EnRr* this, PlayState* play);
void EnRr_UnderwaterVacuum(EnRr* this, PlayState* play);
void EnRr_ScoopPlayer(EnRr* ths, PlayState* play);
void EnRr_GrabPlayer(EnRr* this, PlayState* play);
void EnRr_ThrowPlayer(EnRr* this, PlayState* play);
void EnRr_Death(EnRr* this, PlayState* play);
void EnRr_Retreat(EnRr* this, PlayState* play);
void EnRr_Stunned(EnRr* this, PlayState* play);

void EnRr_InitBodySegments(EnRr* this, PlayState* play);
void EnRr_Damage(EnRr* this, PlayState* play);

const ActorInit En_Rr_InitVars = {
    ACTOR_EN_RR,
    ACTORCAT_ENEMY,
    FLAGS,
    OBJECT_RR,
    sizeof(EnRr),
    (ActorFunc)EnRr_Init,
    (ActorFunc)EnRr_Destroy,
    (ActorFunc)EnRr_Update,
    (ActorFunc)EnRr_Draw,
    NULL,
};

static ColliderCylinderInitType1 sCylinderInit1 = {
    {
        COLTYPE_HIT0,
        AT_NONE,
        AC_ON | AC_TYPE_PLAYER,
        OC1_ON | OC1_TYPE_ALL,
        COLSHAPE_CYLINDER,
    },
    {
        ELEMTYPE_UNK1,
        { 0xFFCFFFFF, 0x00, 0x08 },
        { 0xFFCFFFFF, 0x00, 0x00 },
        TOUCH_ON | TOUCH_SFX_NONE,
        BUMP_ON | BUMP_HOOKABLE,
        OCELEM_ON,
    },
    { 30, 55, 0, { 0, 0, 0 } },
};

// Changed collider from cylinder to sphere; this results in much more visually consistent grabs.
static ColliderJntSphElementInit sbodySphElementsInit[4] = {
    {
        {
            ELEMTYPE_UNK1,
            { 0xFFCFFFFF, 0x00, 0x08 },
            { 0xFFCFFFFF, 0x00, 0x00 },
            TOUCH_ON | TOUCH_SFX_NONE,
            BUMP_ON | BUMP_HOOKABLE,
            OCELEM_ON,
        },
        { 0, { { 0, 0, 0 }, 30 }, 0 },
    },
    {
        {
            ELEMTYPE_UNK1,
            { 0xFFCFFFFF, 0x00, 0x08 },
            { 0xFFCFFFFF, 0x00, 0x00 },
            TOUCH_ON | TOUCH_SFX_NONE,
            BUMP_ON | BUMP_HOOKABLE,
            OCELEM_ON,
        },
        { 0, { { 0, 0, 0 }, 30 }, 0 },
    },
    {
        {
            ELEMTYPE_UNK1,
            { 0xFFCFFFFF, 0x00, 0x08 },
            { 0xFFCFFFFF, 0x00, 0x00 },
            TOUCH_ON | TOUCH_SFX_NONE,
            BUMP_ON | BUMP_HOOKABLE,
            OCELEM_ON,
        },
        { 0, { { 0, 0, 0 }, 30 }, 0 },
    },
    // Mouth
    {
        {
            ELEMTYPE_UNK0,
            { 0xFFCFFFFF, 0x00, 0x08 },
            { 0xFFCFFFFF, 0x00, 0x00 },
            TOUCH_ON | TOUCH_SFX_NONE,
            BUMP_ON | BUMP_HOOKABLE,
            OCELEM_ON,
        },
        { 0, { { 0, 0, 0 }, 15 }, 0 },
    },
};

static ColliderJntSphInit sEnRrJntSphInit = {
    {
        COLTYPE_HIT0,
        AT_NONE,
        AC_ON | AC_TYPE_PLAYER,
        OC1_ON | OC1_NO_PUSH | OC1_TYPE_ALL,
        OC2_TYPE_1,
        COLSHAPE_JNTSPH,
    },
    4,
    sbodySphElementsInit,
};

static DamageTable sDamageTable = {
    /* Deku nut      */ DMG_ENTRY(0, RR_DMG_NONE),
    /* Deku stick    */ DMG_ENTRY(2, RR_DMG_NORMAL),
    /* Slingshot     */ DMG_ENTRY(1, RR_DMG_NORMAL),
    /* Explosive     */ DMG_ENTRY(2, RR_DMG_NORMAL),
    /* Boomerang     */ DMG_ENTRY(0, RR_DMG_STUN),
    /* Normal arrow  */ DMG_ENTRY(2, RR_DMG_NORMAL),
    /* Hammer swing  */ DMG_ENTRY(1, RR_DMG_HAMMER),
    /* Hookshot      */ DMG_ENTRY(0, RR_DMG_NONE),
    /* Kokiri sword  */ DMG_ENTRY(1, RR_DMG_NORMAL),
    /* Master sword  */ DMG_ENTRY(2, RR_DMG_NORMAL),
    /* Giant's Knife */ DMG_ENTRY(4, RR_DMG_NORMAL),
    /* Fire arrow    */ DMG_ENTRY(4, RR_DMG_FIRE),
    /* Ice arrow     */ DMG_ENTRY(4, RR_DMG_ICE),
    /* Light arrow   */ DMG_ENTRY(15, RR_DMG_LIGHT_ARROW),
    /* Unk arrow 1   */ DMG_ENTRY(4, RR_DMG_WIND_ARROW),
    /* Unk arrow 2   */ DMG_ENTRY(15, RR_DMG_SHDW_ARROW),
    /* Unk arrow 3   */ DMG_ENTRY(15, RR_DMG_SPRT_ARROW),
    /* Fire magic    */ DMG_ENTRY(4, RR_DMG_FIRE),
    /* Ice magic     */ DMG_ENTRY(3, RR_DMG_ICE),
    /* Light magic   */ DMG_ENTRY(10, RR_DMG_LIGHT_MAGIC),
    /* Shield        */ DMG_ENTRY(0, RR_DMG_NONE),
    /* Mirror Ray    */ DMG_ENTRY(0, RR_DMG_NONE),
    /* Kokiri spin   */ DMG_ENTRY(1, RR_DMG_NORMAL),
    /* Giant spin    */ DMG_ENTRY(4, RR_DMG_NORMAL),
    /* Master spin   */ DMG_ENTRY(2, RR_DMG_NORMAL),
    /* Kokiri jump   */ DMG_ENTRY(2, RR_DMG_NORMAL),
    /* Giant jump    */ DMG_ENTRY(8, RR_DMG_NORMAL),
    /* Master jump   */ DMG_ENTRY(4, RR_DMG_NORMAL),
    /* Unknown 1     */ DMG_ENTRY(10, RR_DMG_SPRT_ARROW),
    /* Unblockable   */ DMG_ENTRY(0, RR_DMG_NONE),
    /* Hammer jump   */ DMG_ENTRY(2, RR_DMG_HAMMER),
    /* Unknown 2     */ DMG_ENTRY(0, RR_DMG_NONE),
};

static CollisionCheckInfoInit sColChkInfoInit = { 6, 30, 55, 90 };

static InitChainEntry sInitChain[] = {
    ICHAIN_S8(naviEnemyId, 0x37, ICHAIN_CONTINUE),
    ICHAIN_U8(targetMode, 2, ICHAIN_CONTINUE),
    ICHAIN_F32(targetArrowOffset, 30, ICHAIN_STOP),
};

void EnRr_Init(Actor* thisx, PlayState* play) {
    EnRr* this = THIS;

    Actor_ProcessInitChain(&this->actor, sInitChain);
    Collider_InitCylinder(play, &this->cylinder);
    Collider_SetCylinderType1(play, &this->cylinder, &this->actor, &sCylinderInit1);
    Collider_InitJntSph(play, &this->bodySph);
    Collider_SetJntSph(play, &this->bodySph, &this->actor, &sEnRrJntSphInit, this->bodySphItems);
    this->bodySph.elements[0].dim.worldSphere.radius = sEnRrJntSphInit.elements[0].dim.modelSphere.radius;
    this->bodySph.elements[1].dim.worldSphere.radius = sEnRrJntSphInit.elements[1].dim.modelSphere.radius;
    this->bodySph.elements[2].dim.worldSphere.radius = sEnRrJntSphInit.elements[2].dim.modelSphere.radius;
    this->bodySph.elements[3].dim.worldSphere.radius = sEnRrJntSphInit.elements[3].dim.modelSphere.radius;

    CollisionCheck_SetInfo(&this->actor.colChkInfo, &sDamageTable, &sColChkInfoInit);

    this->actor.scale.y = 0.015f;
    this->actor.colChkInfo.health = 6;
    if (this->actor.params == LIKE_LIKE_SMALL) {
        this->actor.scale.y = 0.01125f;
        this->actor.colChkInfo.health = 3;
    } else if (this->actor.params == LIKE_LIKE_GIANT) {
        this->actor.scale.y = 0.0225f;
        this->actor.colChkInfo.health = 8;
    }

    this->maximumHealth = this->actor.colChkInfo.health;

    f32 scaleRatio = this->actor.scale.y / 0.013f;
    this->actor.scale.x = this->actor.scale.z = this->actor.scale.y * SCALE_XZ_MULT;
    this->cylinder.dim.radius *= scaleRatio;
    this->cylinder.dim.height *= scaleRatio;
    this->cylinder.dim.yShift *= scaleRatio;
    this->bodySph.elements[0].dim.worldSphere.radius *= scaleRatio;
    this->bodySph.elements[1].dim.worldSphere.radius *= scaleRatio;
    this->bodySph.elements[2].dim.worldSphere.radius *= scaleRatio;
    this->bodySph.elements[3].dim.worldSphere.radius *= scaleRatio;

    this->actor.colChkInfo.mass *= (!TYPE_STATIONARY(this)) ? scaleRatio : MASS_IMMOVABLE;
    this->actor.gravity = -0.4f;
    this->massRef = this->actor.colChkInfo.mass;
    this->bodyRadiusRef = this->cylinder.dim.radius;
    this->heightRef = this->cylinder.dim.height;
    this->mouthRadiusRef = this->bodySph.elements[3].dim.worldSphere.radius;

    if (TYPE_INVERT(this)) {
        this->actor.gravity = 0.25f;
        this->actor.shape.rot.z = this->actor.world.rot.z = 0x8000;
        this->actor.shape.yOffset = this->cylinder.dim.height / this->actor.scale.y;
        this->cylinder.dim.yShift = 0;
        this->yShiftRef = this->cylinder.dim.yShift;
        ActorShape_Init(&this->actor.shape, this->actor.shape.yOffset, ActorShadow_DrawCircle,
                        this->cylinder.dim.radius);
    }

    if (this->actor.params == LIKE_LIKE_SMALL) {
        this->pitchScale = 0;
    } else if (this->actor.params == LIKE_LIKE_GIANT) {
        this->pitchScale = -2;
    } else {
        this->pitchScale = -1;
    }

    Actor_SetFocus(&this->actor, this->actor.scale.y * 2000.0f);

    this->bodySegCount = ARRAY_COUNT(this->bodySegs) - 1;
    this->phaseCycleCount = 0;
    this->msgEaten = -1;
    this->actionFunc = EnRr_Approach;

    EnRr_InitBodySegments(this, play);
}

void EnRr_Destroy(Actor* thisx, PlayState* play) {
    EnRr* this = THIS;

    Collider_DestroyCylinder(play, &this->cylinder);
    Collider_DestroyJntSph(play, &this->bodySph);
}

void EnRr_SetDefaultMotionParams(EnRr* this, f32 rate) {
    this->phaseCycleTimer = 0;
    this->transitionRate = rate;
    this->segPhaseVelTarget = SEG_PHASE_VEL_DEFAULT;
    this->segPhaseVelRate = this->segPhaseVelTarget / this->transitionRate;
    this->wobbleSizeTarget = WOBBLE_SIZE_DEFAULT;
    this->wobbleSizeRate = this->wobbleSizeTarget / this->transitionRate;
    this->pulseSizeTarget = PULSE_SIZE_DEFAULT;
    this->pulseSizeRate = this->pulseSizeTarget / this->transitionRate;
    this->segWobbleXTarget = WOBBLE_DIFF_X_DEFAULT;
    this->segWobbleXRate = this->segWobbleXTarget / this->transitionRate;
    this->segWobbleZTarget = WOBBLE_DIFF_Z_DEFAULT;
    this->segWobbleZRate = this->segWobbleZTarget / this->transitionRate;
    this->segScaleModYTarget = SCALE_MOD_Y_DEFAULT;
}

void EnRr_CalculateReachAngle(EnRr* this, PlayState* play) {
    Player* player = GET_PLAYER(play);
    // These modify the yDist/xzDist results by factoring the player height and radius.
    s16 playerH = player->cylinder.dim.height;
    s16 playerR = player->cylinder.dim.radius;
    f32 velocityFactor = player->actor.velocity.y < -10.0f ? player->actor.velocity.y * 5.0f : 0.0f;
    s16 i;

    // Uses weighted ratio to determine how much xz and y dist influences reach angle.
    f32 baseHeight = (!TYPE_INVERT(this)) ? this->actor.scale.y * 4000.0f : this->actor.scale.y * 2500.0f;
    f32 reachLengthSum = (!TYPE_INVERT(this)) ? this->actor.scale.y * 3500.0f : this->actor.scale.y * 1500.0f;

    f32 distXZ =
        CLAMP(this->actor.xzDistToPlayer - this->cylinder.dim.radius - playerR, 0.0f, this->actor.scale.x * 3250.0f);
    f32 distY = this->actor.yDistToPlayer + velocityFactor;

    // Biases between top or bottom of player collider based on y pos difference.
    if ((!TYPE_INVERT(this) && distY < 0.0f) || (TYPE_INVERT(this) && distY > baseHeight)) {
        distY *= 1.2f;
    }

    f32 yBias = CLAMP((this->actor.yDistToPlayer + baseHeight * 0.33f) / playerH, -1.2f, 1.2f);
    f32 targetFrac = 0.6f - (yBias * 0.5f);
    f32 targetPlayerY = distY + (playerH * targetFrac);
    f32 finalTargetY = (!TYPE_INVERT(this)) ? targetPlayerY : -targetPlayerY;

    // 1. Define the maximum horizontal distance the Like-Like cares about
    f32 maxReachXZ = this->actor.scale.x * 3250.0f;

    // 2. INVERT the distance.
    // If Link is close (distXZ is 0), invertedXZ is large -> High Bend (Tight Curl)
    // If Link is far (distXZ is 3250), invertedXZ is 0 -> Low Bend (Stretch Out)
    f32 invertedXZ = maxReachXZ - distXZ;

    // 3. Apply your scalar weight
    f32 finalTargetXZ = invertedXZ * 0.4f;

    f32 invScaleY = 1.0f / this->actor.scale.y;
    f32 angleSizeMult = 3.8f * invScaleY;

    f32 angleNumerator = ((baseHeight + reachLengthSum) - finalTargetY + finalTargetXZ) * angleSizeMult;

    this->reachAngle = CLAMP(angleNumerator / this->bodySegCount, 0.0f, 8704.0f);

    for (i = 1; i <= this->bodySegCount; i++) {
        this->bodySegs[i].rotTargetX = this->reachAngle;
    }
}

void EnRr_SetSpeed(EnRr* this, f32 speed) {
    this->actor.speedXZ = speed;
    Audio_PlaySoundTransposed(&this->actor.projectedPos, NA_SE_EN_LIKE_WALK, this->pitchScale);
}

void EnRr_SetupReach(EnRr* this, PlayState* play) {
    Player* player = GET_PLAYER(play);
    s16 playerH = (!TYPE_INVERT(this)) ? -(player->cylinder.dim.height >> 1) : player->cylinder.dim.height >> 1;
    s16 playerR = player->cylinder.dim.radius;
    f32 reachUpMax =
        TYPE_INVERT(this) ? this->actor.scale.y * 18000.0f + playerH : this->actor.scale.y * 12000.0f + playerH;
    f32 velocityFactor = player->actor.velocity.y * 2.0f;
    EnRrStruct* bodySegment;
    EnRrStruct* mouthSegment = &this->bodySegs[this->bodySegCount];
    s16 i;
    s16 bodySegSum = 0;

    // Used for reachHeight, allows modular segment amounts and allocates more height to upper segments.
    for (i = 0; i <= this->bodySegCount; i++) {
        bodySegSum += i;
    }

    this->reachState = 1;
    this->phaseCycleTimer = 0;
    this->phaseCycleCount = 0;
    this->segPhaseVelTarget = SEG_PHASE_VEL_DEFAULT;
    this->segMoveRate = 0.0f;
    if (!this->reachUp) {
        this->bodySph.base.acFlags |= AC_HARD;
    }

    f32 segmentMod = (!TYPE_INVERT(this)) ? 5000.0f : 2000.0f; // Factors Like-Like height into height calculation.
    s32 reachUpFormula =
        ((fabsf(this->actor.yDistToPlayer) - playerH - velocityFactor - this->actor.scale.y * segmentMod) /
         this->bodySegCount) *
        (127.5f * (1.0f - this->actor.scale.y * 27.5f));
    this->reachHeight = !this->reachUp ? 3500.0f / bodySegSum : reachUpFormula;

    if (!this->reachUp) {
        EnRr_CalculateReachAngle(this, play);
    } else {
        this->reachAngle = 0;
        this->wobbleSize = 512.0f;
        this->wobbleSizeTarget = 512.0f;
    }

    for (i = 1; i <= this->bodySegCount; i++) {
        bodySegment = &this->bodySegs[i];
        bodySegment->heightTarget = !this->reachUp ? this->reachHeight * i : this->reachHeight;
        bodySegment->scaleTarget = !this->reachUp ? 0.725f : 0.6f;
        bodySegment->rotTargetX = this->reachAngle;
        bodySegment->rotTargetZ = 0;
    }

    mouthSegment->scaleTarget = 1.5f;
    this->innerMouthScaleTarget = 1.5f;

    // Regular reach uses a flat rate + size mod; reachUp rate is based on height target.
    this->transitionRate = !this->reachUp ? 9.0f + ROUND(SQ(this->actor.scale.y) * 16000.0f)
                                          : this->reachHeight / (175.0f - this->actor.scale.y * 5000.0f);

    this->heightRate = mouthSegment->heightTarget / (this->transitionRate * 1.25f);
    this->rotXRate = !this->reachUp ? 6000 / (this->transitionRate * 0.75f) : 384;
    this->scaleRate1 = (this->bodySegs[1].scale - this->bodySegs[1].scaleTarget) / this->transitionRate;
    this->scaleRate2 = mouthSegment->scaleTarget / (this->transitionRate * 0.5f);

    this->actionFunc = EnRr_Reach;
    Audio_PlaySoundTransposed(&this->actor.projectedPos, NA_SE_EN_LIKE_UNARI, this->pitchScale);
}

void EnRr_SetupUnderwaterVacuum(EnRr* this, PlayState* play) {
    EnRrStruct* bodySegment;
    EnRrStruct* mouthSegment = &this->bodySegs[this->bodySegCount];
    s16 i;

    this->vacuumCooldown = true;
    this->segMoveRate = 0.0f;
    this->transitionRate = 5.0f;
    this->segPhaseVelTarget = 5461;
    this->segPhaseVelRate = (this->segPhaseVelTarget - this->segPhaseVel) / this->transitionRate;
    this->wobbleSizeTarget = 256.0f;
    this->wobbleSizeRate = (this->wobbleSize - this->wobbleSizeTarget) / this->transitionRate;
    this->pulseSizeTarget = 0.175f;
    this->pulseSizeRate = (this->pulseSizeTarget - this->pulseSize) / this->transitionRate;
    this->segScaleModYTarget = 0.1f;
    for (i = 1; i <= this->bodySegCount; i++) {
        bodySegment = &this->bodySegs[i];
        bodySegment->scaleTarget = 0.75f;
        bodySegment->rotTargetX = 0;
        bodySegment->rotTargetZ = 0;
    }

    mouthSegment->scaleTarget = 1.5f;
    this->innerMouthScaleTarget = 1.5f;
    this->scaleRate2 = (mouthSegment->scaleTarget - mouthSegment->scale) / this->transitionRate;
    this->phaseCycleTimer = 0;
    this->phaseCycleCount = 24;
    this->actionFunc = EnRr_UnderwaterVacuum;
    Audio_PlaySoundTransposed(&this->actor.projectedPos, NA_SE_EN_LIKE_UNARI, this->pitchScale);
}

void EnRr_SetupNeutral(EnRr* this, PlayState* play) {
    EnRrStruct* bodySegment;
    EnRrStruct* mouthSegment = &this->bodySegs[this->bodySegCount];
    s16 i;

    this->reachState = 0;
    this->segMoveRate = 0.0f;
    EnRr_SetDefaultMotionParams(this, 40);
    this->phaseCycleTimer = 0;
    this->phaseCycleCount = this->retreat || this->vacuumCooldown ? 14 : 6;
    this->vacuumCooldown = false;
    this->innerMouthScaleTarget = 1.0f;
    this->transitionRate = !this->reachUp ? 12.0f : 24.0f;
    this->rotXRate = !this->reachUp ? this->reachAngle / this->transitionRate : 384.0f;
    this->rotZRate = 1024;
    this->heightRate = mouthSegment->height / this->transitionRate;
    this->scaleRate1 = this->bodySegs[1].scaleTarget / this->transitionRate;
    this->scaleRate2 = mouthSegment->scaleTarget / this->transitionRate;

    for (i = 1; i <= this->bodySegCount; i++) {
        bodySegment = &this->bodySegs[i];
        bodySegment->heightTarget = 0.0f;
        bodySegment->rotTargetX = bodySegment->rotTargetZ = 0;
        bodySegment->scaleTarget = 0.8f;
    }

    this->actionFunc = this->retreat ? EnRr_Retreat : EnRr_Approach;
}

void EnRr_SetGrabParams(EnRr* this, Player* player, PlayState* play) {
    EnRrStruct* bodySegment;
    EnRrStruct* mouthSegment = &this->bodySegs[this->bodySegCount];
    s16 i;

    // Puts player item/weapon away to prevent clipping issues.
    player->heldItemId = ITEM_NONE;
    player->heldItemAction = PLAYER_IA_NONE;
    Player_SetEquipmentData(play, player);
    player->swallowed = true;

    this->grabState = 1;
    this->phaseCycleTimer = 0;
    this->phaseCycleCount = (this->actor.params == LIKE_LIKE_SMALL && LINK_IS_ADULT) ? 32 : 8;
    this->soundEatCounter = 0;
    this->segPhaseVel = CLAMP_MIN(this->segPhaseVel, 1536);
    this->soundTimer = 0x8000 / this->segPhaseVel;
    this->struggleCounter = 0;
    this->struggleSpeedup = 0;
    // Falling into a Like-Like's mouth makes it temporarily harder to break free.
    this->catchPenalty = player->actor.velocity.y < -4.0f ? 1 : 2 + abs((s16)player->actor.velocity.y) >> 1;
    this->catchPenalty = (TYPE_DRAIN(this)) && this->catchPenalty < 6 ? 6 : this->catchPenalty;
    this->stolenLife = 0;
    this->grabEject = 0;
    this->playerInside = false;
    this->throwStrength = 0;
    this->damageRelease = 0;
    this->slimePlayer = false;
    this->slimeCounter = 0;
    this->actor.flags &= ~ACTOR_FLAG_ATTENTION_ENABLED;
    this->cylinder.base.ocFlags1 &= ~OC1_TYPE_PLAYER;
    this->bodySph.base.ocFlags1 &= ~OC1_TYPE_PLAYER;
    this->ocPlayerTimer = 20;
    this->reachState = 0;
    this->actor.colChkInfo.mass = MASS_IMMOVABLE; // Immovable while holding player.
    player->cylinder.base.ocFlags1 &= ~OC1_ON;    // Prevents other actors from shoving player out.
    this->segMoveRate = this->actor.speedXZ = 0.0;
    this->wobbleSize = this->reachUp ? 0.0f : this->wobbleSize;
    this->segWobblePhaseDiffX = 4.0f;
    this->segWobbleXTarget = 4.0f;
    this->segWobblePhaseDiffZ = 3.0f;
    this->segWobbleZTarget = 3.0f;
    this->segScaleModYTarget =
        TYPE_DRAIN(this) || (this->actor.params == LIKE_LIKE_SMALL && LINK_IS_ADULT) ? SCALE_MOD_Y_DEFAULT : 0.05f;

    for (i = 1; i <= this->bodySegCount; i++) {
        bodySegment = &this->bodySegs[i];
        bodySegment->heightTarget = 0.0f;
        bodySegment->rotTargetX = this->bodySegs[i].rotTargetZ = 0;
        bodySegment->scaleTarget = 1.0f;
    }

    mouthSegment->scaleTarget = 0.85f;
    this->innerMouthScaleTarget = 0.85f;

    // Grabs during retreat make it temporarily harder to break free.
    if (this->retreat) {
        this->phaseCycleCount = 24;
        this->catchPenalty += 5;
    }
    // Tacks on half the remaining timer from UnderwaterVacuum if caught during it.
    if (this->vacuumCooldown) {
        this->catchPenalty += this->phaseCycleCount >> 1;
    }

    bodySegment = &this->bodySegs[1];
    this->transitionRate = !this->reachUp ? 15.0f : CLAMP_MIN(mouthSegment->height / 100.0f, 1.0f);
    this->rotXRate = !this->reachUp ? this->reachAngle / this->transitionRate : 384.0f;
    this->heightRate = mouthSegment->height / this->transitionRate;
    this->scaleRate1 = (bodySegment->scaleTarget - bodySegment->scale) / (this->transitionRate * 1.75f);
    this->scaleRate2 = mouthSegment->scaleTarget / (this->transitionRate * 0.5f);
}

void EnRr_SetupGrabPlayer(EnRr* this, Player* player, PlayState* play) {
    bool facingCondition =
        (Player_IsFacingActor(&this->actor, 0x4000, play) && Actor_IsFacingPlayer(&this->actor, 0x4000)) ||
        (!Player_IsFacingActor(&this->actor, 0x4000, play) && !Actor_IsFacingPlayer(&this->actor, 0x4000));
    this->storedPlayerIsFacing = facingCondition;
    EnRr_SetGrabParams(this, player, play);

    if (!TYPE_INVERT(this)) {
        // Applies falling player y velocity to initial offset for much smoother grab.
        this->swallowOffset = player->actor.velocity.y < -4.0f ? player->actor.velocity.y : 0.0f;
    } else {
        this->swallowOffset = -player->cylinder.dim.height;
    }

    Audio_PlaySoundTransposed(&this->actor.projectedPos, NA_SE_EN_LIKE_DRINK, this->pitchScale);
    this->actionFunc = EnRr_GrabPlayer;
}

void EnRr_SetupScoopPlayer(EnRr* this, PlayState* play) {
    Player* player = GET_PLAYER(play);
    EnRrStruct* bodySegment;
    EnRrStruct* mouthSegment = &this->bodySegs[this->bodySegCount];
    s16 i;
    s16 bodySegSum = 0;
    bool facingCondition =
        (Player_IsFacingActor(&this->actor, 0x4000, play) && Actor_IsFacingPlayer(&this->actor, 0x4000)) ||
        (!Player_IsFacingActor(&this->actor, 0x4000, play) && !Actor_IsFacingPlayer(&this->actor, 0x4000));

    for (i = 0; i <= this->bodySegCount; i++) {
        bodySegSum += i;
    }

    this->reachState = 1;
    this->phaseCycleTimer = 0x4000 - 364000.0f * this->actor.scale.y;
    this->phaseCycleCount = 1;
    this->actor.speedXZ = 0.0f;
    this->segMoveRate = 0.0f;
    this->reachHeight = 0.0f;
    this->actor.flags &= ~ACTOR_FLAG_ATTENTION_ENABLED;
    this->cylinder.base.ocFlags1 &= ~OC1_TYPE_PLAYER;
    this->bodySph.base.ocFlags1 &= ~OC1_TYPE_PLAYER;
    this->ocPlayerTimer = 20;
    this->vacuumCooldown = true; // Mostly just to set the sped up swallowOffset.
    this->storedPlayerIsFacing = facingCondition;
    EnRr_CalculateReachAngle(this, play);
    this->reachAngle = CLAMP(this->reachAngle, 3072, 7168);

    for (i = 1; i <= this->bodySegCount; i++) {
        bodySegment = &this->bodySegs[i];
        bodySegment->heightTarget = this->reachHeight * i;
        bodySegment->scaleTarget = 0.8f;
        bodySegment->rotTargetX = this->reachAngle;
        bodySegment->rotTargetZ = 0;
    }

    mouthSegment->scaleTarget = 1.5f;
    this->innerMouthScaleTarget = 1.5f;

    if (!TYPE_INVERT(this)) {
        s16 offsetSizeMod = 1.0f - (this->actor.scale.y / 0.0225f);
        this->swallowOffset =
            player->actor.velocity.y < -4.0f ? player->actor.velocity.y : -player->cylinder.dim.height * 0.33f;
    } else {
        this->swallowOffset = -player->cylinder.dim.height;
    }

    this->transitionRate = CLAMP_MIN(ROUND(SQ(this->actor.scale.y) * 16000.0f), 5.0f);

    this->heightRate = mouthSegment->heightTarget / this->transitionRate;
    this->rotXRate = (this->reachAngle * 1.5f) / (this->transitionRate);
    this->scaleRate1 = ABS(this->bodySegs[1].scale - this->bodySegs[1].scaleTarget) / this->transitionRate;
    this->scaleRate2 = mouthSegment->scaleTarget / this->transitionRate;

    this->actionFunc = EnRr_ScoopPlayer;
    Audio_PlaySoundTransposed(&this->actor.projectedPos, NA_SE_EN_LIKE_UNARI, this->pitchScale);
    Audio_PlaySoundTransposed(&this->actor.projectedPos, NA_SE_EN_LIKE_DRINK, this->pitchScale);
}

void EnRr_SetupDamage(EnRr* this) {
    EnRrStruct* segment;
    s16 i;

    this->reachState = 0;
    this->invincibilityTimer =
        (this->actor.colChkInfo.damageEffect == RR_DMG_HAMMER || this->actor.colChkInfo.damageEffect == RR_DMG_ICE)
            ? 80
            : 38;
    this->phaseCycleCount = 0;
    Actor_SetColorFilter(&this->actor, 0x4000, 255, 0, this->invincibilityTimer);
    this->segMoveRate = 0.0f;
    this->segPhaseVel = 512;
    this->segPhaseVelTarget = SEG_PHASE_VEL_DEFAULT;
    this->wobbleSizeTarget = 0.0f;
    this->pulseSizeTarget = 0.0f;
    this->transitionRate = 12.0f;
    this->heightRate = this->bodySegs[this->bodySegCount].height / this->transitionRate;
    this->scaleRate1 = this->scaleRate2 = 0.175f;
    this->rotXRate = 384;
    this->rotZRate = 2048;

    for (i = 1; i <= this->bodySegCount; i++) {
        segment = &this->bodySegs[i];
        segment->heightTarget = 0.0f;
        segment->scaleTarget = 0.8f;
        segment->rotTargetX = this->bodySegs[i].rotTargetZ = 0;
    }

    this->actionFunc = EnRr_Damage;
    Audio_PlaySoundTransposed(&this->actor.projectedPos, NA_SE_EN_LIKE_DAMAGE, this->pitchScale);
}

void EnRr_SetupReleasePlayer(EnRr* this, PlayState* play) {
    Player* player = GET_PLAYER(play);
    f32 launchXZ;
    f32 launchY;

    player->actor.parent = NULL;
    player->av2.actionVar2 = 0;

    this->actor.flags |= ACTOR_FLAG_ATTENTION_ENABLED;
    this->reachUp = false;
    this->phaseCycleCount = 4;
    this->regrabTimer = 50;
    this->segMoveRate = 0.0f;
    this->segPhaseVel = SEG_PHASE_VEL_DEFAULT;
    EnRr_SetDefaultMotionParams(this, 20);
    this->actor.colChkInfo.mass =
        (!TYPE_STATIONARY(this)) ? this->massRef : MASS_IMMOVABLE; // Return mass to original state.
    player->cylinder.base.ocFlags1 |= OC1_ON;

    if ((this->msgEaten != -1) && (Message_GetState(&play->msgCtx) == TEXT_STATE_NONE)) {
        switch (this->msgEaten) {
            case 0: // Sword
                Message_StartTextbox(play, 0x305F, NULL);
                break;
            case 1: // Shield
                Message_StartTextbox(play, 0x305F, NULL);
                break;
            case 2: // Tunic
                Message_StartTextbox(play, 0x3060, NULL);
                break;
            case 3: // Boots
                Message_StartTextbox(play, 0x3060, NULL);
                break;
            case 4: // Bottle
                Message_StartTextbox(play, 0x3061, NULL);
                break;
            case 5: // Item
                Message_StartTextbox(play, 0x3061, NULL);
                break;
        }
    }
    this->msgEaten = -1;

    if (this->actor.params == MAGIC_LIKE) {
        Magic_Reset(play);
    }

    if (!TYPE_INVERT(this) && this->grabEject < 10) {
        f32 throwModXZ = this->actor.colChkInfo.health <= 0 ? 0.0f : 0.5f + (f32)this->throwStrength * 0.1f;
        f32 throwModY = this->actor.colChkInfo.health <= 0 ? -4.0f : 0.5f + (f32)this->throwStrength * 0.1f;
        launchXZ = (this->actor.scale.x * 200.0f + 1.0f) * throwModXZ;
        launchY = (this->actor.scale.x * 375.0f + 1.0f) * throwModY;
    } else {
        launchXZ = 0.0f;
        launchY = this->actor.bgCheckFlags & 0x20 ? -(this->actor.scale.x * 1200.0f + 1.0f) : 0.0f;
    }

    player->actor.world.pos.x += launchXZ * Math_SinS(this->actor.shape.rot.y);
    player->actor.world.pos.y += launchY;
    player->actor.world.pos.z += launchXZ * Math_CosS(this->actor.shape.rot.y);
    player->actor.world.rot.x = player->actor.shape.rot.x = 0;
    GameInteractor_GetLinkSize(GI_LINK_SIZE_RESET);

    func_8002F6D4(play, &this->actor, launchXZ, this->actor.shape.rot.y, launchY, 0);
    u16 releaseSound = this->slimePlayer ? NA_SE_EN_AWA_BREAK : NA_SE_EN_LIKE_THROW;
    Audio_StopSfxById(NA_SE_EN_OCTAROCK_BUBLE);
    Audio_StopSfxById(NA_SE_EN_LIKE_UNARI);
    Audio_PlaySoundTransposed(&this->actor.projectedPos, releaseSound, this->pitchScale);
    CollisionCheck_SpawnWaterDroplets(play, &this->bodySphPos[3]);

    if (this->damageRelease != 0) {
        EnRr_SetupDamage(this);
    }
}

void EnRr_SetupThrowPlayer(EnRr* this, PlayState* play) {
    Player* player = GET_PLAYER(play);
    EnRrStruct* bodySegment;
    EnRrStruct* mouthSegment = &this->bodySegs[this->bodySegCount];
    s16 i;
    s16 bodySegSum = 0;

    for (i = 0; i <= this->bodySegCount; i++) {
        bodySegSum += i;
    }

    player->av2.actionVar2 = 0;

    if (this->damageRelease > 0) {
        this->throwStrength = 0;
    }

    this->reachState = 1;
    this->segMoveRate = 0.0f;
    this->reachAngle = (13312.0f / this->bodySegCount) * (1.35f - (f32)this->throwStrength * 0.07f);
    this->reachHeight = (3000.0f / bodySegSum) * (0.3f + (f32)this->throwStrength * 0.14f);
    this->transitionRate = this->slimeCounter > 600 ? 28.0f : 4.0f + (this->throwStrength * 3);

    if ((this->slimeCounter >= 100 || player->slimeTimer != 0) && this->throwStrength == 5) {
        this->slimePlayer = true;
        player->slimeTimer += this->slimeCounter;
    }

    if (!(this->reachUp && TYPE_INVERT(this))) {
        player->actor.shape.rot.y =
            this->storedPlayerIsFacing ? BINANG_ROT180(this->actor.world.rot.y) : this->actor.world.rot.y;
    }
    for (i = 1; i <= this->bodySegCount; i++) {
        bodySegment = &this->bodySegs[i];
        bodySegment->heightTarget = this->reachHeight * i;
        bodySegment->rotTargetX = (!TYPE_INVERT(this)) ? this->reachAngle : 0.0f;
        bodySegment->rotTargetZ = 0;
        bodySegment->scaleTarget = 0.85f;
    }

    mouthSegment->scaleTarget = this->slimeCounter > 600 ? 0.625f : 0.75f;
    this->innerMouthScaleTarget = this->slimeCounter > 600 ? 0.725f : 0.85f;

    bodySegment = &this->bodySegs[1];
    this->heightRate = mouthSegment->heightTarget / this->transitionRate;
    this->scaleRate1 = (bodySegment->scale - bodySegment->scaleTarget);
    this->scaleRate2 = mouthSegment->scaleTarget / (this->transitionRate * 0.5f);
    this->rotZRate = this->wobbleSize / (this->transitionRate * 0.8f);
    this->rotXRate = (!TYPE_INVERT(this)) ? this->reachAngle / (this->transitionRate * 0.8f) : 384.0f;

    Audio_StopSfxById(NA_SE_EN_BIRI_BUBLE);
    Audio_StopSfxById(NA_SE_EV_WATER_BUBBLE);

    if (this->slimeCounter > 600) {
        Audio_PlaySoundTransposed(&this->actor.projectedPos, NA_SE_EN_LIKE_UNARI, this->pitchScale);
    } else {
        Audio_PlaySoundTransposed(&this->actor.projectedPos, NA_SE_EN_OCTAROCK_BUBLE, this->pitchScale - 2);
    }
    this->actionFunc = EnRr_ThrowPlayer;
}

void EnRr_SetupApproach(EnRr* this) {
    EnRrStruct* segment;
    s16 i;

    this->segMoveRate = 0.0f;
    this->segPhaseVelTarget = SEG_PHASE_VEL_DEFAULT;
    this->wobbleSizeTarget = WOBBLE_SIZE_DEFAULT;
    this->pulseSizeTarget = PULSE_SIZE_DEFAULT;

    for (i = 1; i <= this->bodySegCount; i++) {
        segment = &this->bodySegs[i];
        segment->heightTarget = 0.0f;
        segment->scaleTarget = 0.8f;
        segment->rotTargetX = segment->rotTargetZ = 0;
    }

    this->actionFunc = EnRr_Approach;
}

void EnRr_SetupDeath(EnRr* this) {
    EnRrStruct* segment;
    s16 i;

    this->frameCount = 0;
    this->shrinkRate = 0.0f;
    this->segScaleModY = 0.0f;
    this->segScaleModYTarget = 0.0f;
    this->heightRate = 100.0f;
    this->scaleRate1 = this->scaleRate2 = 0.175f;
    this->rotXRate = 384;
    this->rotZRate = 1024;
    Actor_SetColorFilter(&this->actor, 0x4000, 255, 0, 40);
    this->segMoveRate = 0.0f;

    for (i = 0; i <= this->bodySegCount; i++) {
        segment = &this->bodySegs[i];
        segment->heightTarget = 0.0f;
        segment->rotTargetX = 0;
        segment->rotTargetZ = 0;
    }

    this->actionFunc = EnRr_Death;
    Audio_PlaySoundTransposed(&this->actor.projectedPos, NA_SE_EN_LIKE_DEAD, this->pitchScale);
    this->actor.flags &= ~ACTOR_FLAG_ATTENTION_ENABLED;
}

void EnRr_SetupStunned(EnRr* this, PlayState* play) {
    EnRrStruct* segment;
    s16 i;

    this->actor.speedXZ = 0.0f;
    this->segMovePhase = 0;
    this->segPhaseVel = 0;
    this->wobbleSize = 0.0f;
    this->pulseSize = 0.0f;
    this->segWobblePhaseDiffX = 0.0f;
    this->segWobblePhaseDiffZ = 0.0f;
    EnRr_SetDefaultMotionParams(this, 100);

    for (i = 1; i <= this->bodySegCount; i++) {
        segment = &this->bodySegs[i];
        segment->scale = 0.8f;
        segment->scaleTarget = 0.8f;
        segment->rotTargetX = 0;
        segment->rotTargetY = 0;
        segment->rotTargetZ = 0;
    }

    this->actionFunc = EnRr_Stunned;
}

s32 EnRr_CollisionCheck(EnRr* this, PlayState* play) {
    ColliderCylinder* acHit;
    s32 flag1 = (this->cylinder.base.acFlags & AC_HIT) != 0 && this->invincibilityTimer == 0;
    s32 flag2 = (this->bodySph.base.acFlags & AC_HIT) != 0 && this->invincibilityTimer == 0;

    if (flag1 || flag2) {
        if (flag1) {
            acHit = &this->cylinder;
        } else if (flag2) {
            acHit = &this->bodySph;
            if (this->bodySph.base.acFlags & AC_HARD) {
                return;
            }
        }

        this->cylinder.base.acFlags &= ~AC_HIT;
        this->bodySph.base.acFlags &= ~AC_HIT;

        Actor_SetDropFlag(&this->actor, &this->cylinder.info, 1);
        if ((this->actionFunc == EnRr_GrabPlayer || this->actionFunc == EnRr_ScoopPlayer) &&
            Actor_ApplyDamage(&this->actor)) {
            // Took damage but survived
            if (this->damageRelease == 0) {
                this->damageRelease = this->actor.colChkInfo.damage;
            }
            this->invincibilityTimer = 20;
            if (this->playerInside) {
                EnRr_SetupThrowPlayer(this, play);
            } else {
                EnRr_SetupReleasePlayer(this, play);
            }
            return true;
        } else if (!Actor_ApplyDamage(&this->actor)) {
            Enemy_StartFinishingBlow(play, &this->actor);
            if (this->actor.colChkInfo.damageEffect == RR_DMG_ICE) {
                this->cylinder.base.acFlags &= ~AC_ON;
                this->bodySph.base.acFlags &= ~AC_ON;
                EnRr_SetupStunned(this, play);
            } else {
                if (this->actionFunc == EnRr_GrabPlayer || this->actionFunc == EnRr_ScoopPlayer) {
                    this->throwStrength = 0;
                    EnRr_SetupReleasePlayer(this, play);
                }
                EnRr_SetupDeath(this);
            }
        } else if (this->actor.colChkInfo.damageEffect == RR_DMG_STUN) {
            Actor_SetColorFilter(&this->actor, 0, 255, 0, 80);
            this->stunTimer = 80;
            EnRr_SetupStunned(this, play);
        } else if (this->actor.colChkInfo.damageEffect == RR_DMG_ICE) {
            Actor_SetColorFilter(&this->actor, 0, 255, 0, 80);
            this->stunTimer = 80;
            EnRr_SetupStunned(this, play);
        } else if (this->actor.colChkInfo.damageEffect == RR_DMG_NONE) {
            return false;
        } else {
            this->stunTimer = 0;
            EnRr_SetupDamage(this);
        }
        return true;
    }
    return false;
}

void EnRr_PlayerCollisionCheck(EnRr* this, PlayState* play) {
    Player* player = GET_PLAYER(play);

    if ((this->regrabTimer == 0) && (this->actor.colorFilterTimer == 0) && !(player->swallowed) &&
        (player->invincibilityTimer == 0) &&
        (((this->cylinder.base.ocFlags1 & OC1_HIT) || (this->bodySph.base.ocFlags1 & OC1_HIT)) &&
         ((this->cylinder.base.oc == &player->actor) || (this->bodySph.base.oc == &player->actor)))) {
        this->cylinder.base.ocFlags1 &= ~OC1_HIT;
        this->bodySph.base.ocFlags1 &= ~OC1_HIT;

        if (play->grabPlayer(play, player)) {
            player->actor.parent = &this->actor;
            if (this->actionFunc != EnRr_Reach) {
                EnRr_SetupScoopPlayer(this, play);
            } else {
                EnRr_SetupGrabPlayer(this, player, play);
            }
        }
    }
}

void EnRr_InitBodySegments(EnRr* this, PlayState* play) {
    EnRrStruct* segment;
    s16 i;

    this->segMovePhase = 0;
    this->innerMouthScaleTarget = 1.0f;
    this->rotXRate = 368;
    this->rotZRate = 1024;
    EnRr_SetDefaultMotionParams(this, 100);

    for (i = 0; i < ARRAY_COUNT(this->bodySegs); i++) {
        segment = &this->bodySegs[i];
        segment->scale = 0.8f;
        segment->scaleTarget = 0.8f;
        segment->scaleMod = 0.0f;
        segment->rotTargetX = 0;
        segment->rotTargetY = 0;
        segment->rotTargetZ = 0;
    }
    this->bodySegs[0].scaleTarget = 1.0f;
}

void EnRr_UpdateBodySegments(EnRr* this, PlayState* play) {
    EnRrStruct* segment;
    s16 i;
    s16 pulseIncrement = 0x10000 / this->bodySegCount;
    u16 phaseDiffXIncrement = this->segWobblePhaseDiffX * 0x1000;
    u16 phaseDiffZIncrement = this->segWobblePhaseDiffZ * 0x1000;
    f32 wobbleScale = (this->wobbleSize * 4.0f) / this->bodySegCount;

    if (this->actionFunc != EnRr_Death) {
        for (i = 1; i <= this->bodySegCount; i++) {
            u16 phasePulse = this->segMovePhase + i * pulseIncrement;
            f32 pulseRadians = (phasePulse * (2.0f * M_PI)) / 65536.0f;

            u16 ySquish = this->segMovePhase + i * (pulseIncrement >> 1);
            f32 ySquishRadians = (ySquish * (2.0f * M_PI)) / 65536.0f;

            segment = &this->bodySegs[i];
            segment->scaleMod = sinf(pulseRadians) * this->pulseSize;
            segment->ySquishMod = sinf(ySquishRadians) * this->segScaleModY;
        }

        if (this->actionFunc != EnRr_Reach && this->actionFunc != EnRr_ScoopPlayer &&
            this->actionFunc != EnRr_ThrowPlayer) {
            for (i = 1; i <= this->bodySegCount; i++) {
                u16 phaseDiffX = this->segMovePhase + i * phaseDiffXIncrement;
                u16 phaseDiffZ = this->segMovePhase + i * phaseDiffZIncrement;
                f32 diffXRadians = (phaseDiffX * (2.0f * M_PI)) / 65536.0f;
                f32 diffZRadians = (phaseDiffZ * (2.0f * M_PI)) / 65536.0f;

                segment = &this->bodySegs[i];
                segment->rotTargetX = sinf(diffXRadians) * wobbleScale;
                segment->rotTargetZ = cosf(diffZRadians) * wobbleScale;
            }
        }
    }

    if (this->stunTimer == 0) {
        this->segMovePhase += this->segPhaseVel;
    }
}

// Dynamic proximity detection scales off actor plus the player's collider dimensions
void EnRr_ReachDetect(EnRr* this, PlayState* play) {
    Player* player = GET_PLAYER(play);
    s16 playerH = player->cylinder.dim.height >> 1;
    s16 playerR = player->cylinder.dim.radius;
    f32 deltaX = this->actor.xzDistToPlayer;
    f32 deltaY = this->actor.yDistToPlayer;
    f32 scaleX = this->actor.scale.x;
    f32 scaleY = this->actor.scale.y;
    f32 velocityFactor = player->actor.velocity.y < -5.0f ? player->actor.velocity.y * 15.0f : 0.0f;
    Vec3f hitPos;
    CollisionPoly* poly;
    s32 bgId;
    bool lineTest = (BgCheck_EntityLineTest1(&play->colCtx, &player->actor.world.pos, &this->bodySphPos[3], &hitPos,
                                             &poly, true, true, true, true, &bgId));

    // 1. Check cycle timer, if player isn't already grabbed, and if Skull Mask is on.
    // 2. Reach upward action is checked first in a narrow range above Like Like.
    // 3. Normal reach is checked next in a cone within the Like Like's exact range.
    // 4. For stationary types, do additional check for underwater vacuum.
    if ((this->phaseCycleCount == 0) && !(player->swallowed) && (Player_GetMask(gPlayState) != PLAYER_MASK_SKULL)) {
        if (!(TYPE_INVERT(this))) {
            if ((deltaY < scaleY * 12000.0f + playerH - velocityFactor) && (deltaY > scaleY * 7000.0f + playerH) &&
                (deltaX < scaleX * 2750.0f + playerR)) {
                this->reachUp = true;
                EnRr_SetupReach(this, play);
            } else if ((Actor_IsFacingPlayer(&this->actor, 0x5000)) &&
                       (deltaY < scaleY * 7000.0f + playerH - velocityFactor) &&
                       (deltaY > -(scaleY * 3500.0f + playerH)) && (deltaX < scaleX * 6250.0f + playerR)) {
                this->reachUp = false;
                EnRr_SetupReach(this, play);
            } else if ((TYPE_STATIONARY(this)) && (this->actor.yDistToWater > this->heightRef) && (deltaY < 400.0f) &&
                       (deltaY > this->heightRef) && (deltaX < scaleX * 10000.0f + playerR) &&
                       (player->actor.bgCheckFlags & 0x20)) {
                EnRr_SetupUnderwaterVacuum(this, play);
            }
        } else {
            if ((deltaY > -(scaleY * 15000.0f + playerH)) && (deltaY < -(scaleY * 5500.0f + playerH)) &&
                (deltaX < scaleX * 3250.0f + playerR)) {
                this->reachUp = true;
                EnRr_SetupReach(this, play);
            } else if ((Actor_IsFacingPlayer(&this->actor, 0x5000)) && (deltaY > -(scaleY * 5500.0f + playerH)) &&
                       (deltaY < scaleY * 3000.0f + playerH) && (deltaX < scaleX * 6250.0f + playerR)) {
                this->reachUp = false;
                EnRr_SetupReach(this, play);
            } else if ((TYPE_STATIONARY(this)) && (this->actor.yDistToWater > this->heightRef) &&
                       (deltaY > -(400.0f + playerH)) && (deltaY < -this->heightRef) &&
                       (deltaX < scaleX * 10000.0f + playerR) && (player->actor.bgCheckFlags & 0x20)) {
                EnRr_SetupUnderwaterVacuum(this, play);
                // Tries to fall onto the player if approaching but out of reachUp range.
            } else if ((deltaY < -(scaleY * 15000.0f + playerH)) && (deltaX < scaleX * 6250.0f + playerR) &&
                       (this->actor.params != LIKE_LIKE_STATIONARY_INVERT)) {
                this->fallTimer++;
            }
        }
    }
}

void EnRr_Approach(EnRr* this, PlayState* play) {
    Player* player = GET_PLAYER(play);
    this->actor.world.rot.y = this->actor.shape.rot.y;
    // if (this->actor.xyzDistToPlayerSq > SQ(650.0f) && this->segPhaseVel <= 1850) {
    // EnRr_SetupSink(this);
    // }
    //  Faces player if in range.
    if (this->actor.xyzDistToPlayerSq < SQ(650.0f)) {
        Math_SmoothStepToS(&this->actor.shape.rot.y, this->actor.yawTowardsPlayer, 0xA, 0x400, 0);
    }
    // Moves towards player if in range.
    f32 range = (TYPE_INVERT(this) ? 500.0f : 350.0f) + this->actor.scale.y * 5000.0f;

    if (((this->actor.xyzDistToPlayerSq < SQ(range)) || (this->actor.isTargeted)) && !(player->swallowed)) {

        // Only check to reach when in movement range and only every other frame.
        if ((this->frameCount & 1) == 0) {
            EnRr_ReachDetect(this, play);
        }

        if ((this->actor.speedXZ == 0.0f) && (!TYPE_STATIONARY(this))) { // Fixes type triggering SetSpeed function.
            this->segPhaseVelTarget = this->retreat ? 3276 : SEG_PHASE_VEL_DEFAULT;
            this->wobbleSizeTarget = WOBBLE_SIZE_DEFAULT;
            this->pulseSizeTarget = PULSE_SIZE_DEFAULT;
            f32 speed = 2.5f;
            EnRr_SetSpeed(this, speed);
        }
    } else if (this->retreat) {
        this->phaseCycleTimer = 0;
        this->phaseCycleCount = 14;
        this->actionFunc = EnRr_Retreat;
    } else {
        // If out of range, Like-Like will idle.
        Math_StepToS(&this->segPhaseVelTarget, 1820, this->segPhaseVelRate);
        Math_StepToF(&this->wobbleSizeTarget, 0.0f, this->wobbleSizeRate);
        Math_StepToF(&this->pulseSizeTarget, 0.075f, this->pulseSizeRate);
    }
}

// Reach states have been simplified and mainly moves through states when height conditions are met rather than the
// action timer. Reach angle is now fully dynamic and reactive.
void EnRr_Reach(EnRr* this, PlayState* play) {
    EnRrStruct* mouthSegment = &this->bodySegs[this->bodySegCount];
    Player* player = GET_PLAYER(play);
    s16 playerH = (!TYPE_INVERT(this)) ? -(player->cylinder.dim.height >> 1) : player->cylinder.dim.height >> 1;
    s16 playerR = player->cylinder.dim.radius;

    if (Player_GetMask(gPlayState) == PLAYER_MASK_SKULL) {
        EnRr_SetupNeutral(this, play);
    }

    if (!this->reachUp) {
        f32 invScaleY = 1.0f / this->actor.scale.y;
        Math_SmoothStepToS(&this->actor.shape.rot.y, this->actor.yawTowardsPlayer, 4, (s16)(12.0f * invScaleY),
                           (s16)(2.0f * invScaleY));
        EnRr_CalculateReachAngle(this, play);
    }
    this->actor.world.rot.y = this->actor.shape.rot.y;

    switch (this->reachState) {
        case 1:
            if (mouthSegment->height > mouthSegment->heightTarget * 0.8f) {
                mouthSegment->scaleTarget = 0.725f;
                this->innerMouthScaleTarget = 1.0f;
                if (!this->reachUp) {
                    mouthSegment->heightTarget *= 1.25f;
                }
                this->reachState = 2;
            }
            break;
        case 2:
            if (mouthSegment->height == mouthSegment->heightTarget) {
                this->phaseCycleTimer = 0;
                this->phaseCycleCount = 2;
                this->reachState = 3;
            }
            break;
        case 3:
            if (this->phaseCycleCount == 0) {
                EnRr_SetupNeutral(this, play);
            }
            break;
    }
}

void Math3D_Vec3fNormalize(Vec3f* v) {
    float magSq = (v->x * v->x) + (v->y * v->y) + (v->z * v->z);

    if (magSq > 0.0f) {
        float invMag = 1.0f / sqrtf(magSq);
        v->x *= invMag;
        v->y *= invMag;
        v->z *= invMag;
    }
}

void EnRr_UnderwaterVacuum(EnRr* this, PlayState* play) {
    Player* player = GET_PLAYER(play);
    s16 soundMod;
    s16 playerH = this->actor.params != LIKE_LIKE_STATIONARY_INVERT ? -player->cylinder.dim.height / 2
                                                                    : player->cylinder.dim.height;
    f32 scaleOffset = -this->actor.scale.y * 2000.0f;

    f32 invScaleY = 1.0f / this->actor.scale.y;
    Math_SmoothStepToS(&this->actor.shape.rot.y, this->actor.yawTowardsPlayer, 4, (s16)(5.0f * invScaleY),
                       (s16)(2.5f * invScaleY));
    this->actor.world.rot.y = this->actor.shape.rot.y;

    Vec3f playerTargetPos = player->actor.world.pos;
    playerTargetPos.y += playerH;

    Vec3f mouthTargetPos = this->bodySphPos[3];
    mouthTargetPos.y += scaleOffset;

    Vec3f diff;
    Math_Vec3f_Diff(&mouthTargetPos, &playerTargetPos, &diff);

    f32 distSq = SQ(diff.x) + SQ(diff.y) + SQ(diff.z);

    float maxDistSq = SQ(400.0f);

    if ((player->actor.bgCheckFlags & 0x20) && (distSq < maxDistSq)) {
        f32 dist = sqrtf(distSq);

        // Normalize direction
        Math3D_Vec3fNormalize(&diff);

        // Stronger pull when closer
        f32 strengthMod = 0.225f;
        f32 pullRatio = 1.0f - (distSq / maxDistSq); // 1.0 when close, 0.0 at max range
        f32 suctionStrength = pullRatio * 30.0f * strengthMod;

        f32 yMod = this->actor.params == LIKE_LIKE_STATIONARY_INVERT ? 0.25f : 0.3f;

        // Apply force toward mouth
        player->actor.world.pos.x += diff.x * suctionStrength;
        player->actor.velocity.y += diff.y * suctionStrength * yMod;
        player->actor.world.pos.z += diff.z * suctionStrength;
    }

    // Calculate the phase crossing locally
    s16 oldPhase = (s16)(this->segMovePhase - this->segPhaseVel);
    s16 currentPhase = (s16)this->segMovePhase;

    // Fire audio and dust exactly when the visual geometry hits a peak/valley!
    if ((oldPhase ^ currentPhase) < 0) {
        Audio_PlaySoundTransposed(&this->actor.projectedPos, NA_SE_PL_MOVE_BUBBLE, this->pitchScale);
        Actor_SpawnFloorDustRing(play, &this->actor, &this->actor.world.pos, this->cylinder.dim.radius * 0.8f, 8, 5.0f,
                                 250, 10, 1);
        if (this->phaseCycleCount % 2 == 0) {
            Audio_PlaySoundTransposed(&this->actor.projectedPos, NA_SE_EN_OCTAROCK_BUBLE, this->pitchScale);
        }
    }

    if (this->phaseCycleCount == 0) {
        EnRr_SetupNeutral(this, play);
    }
}

void EnRr_GrabPlayerPositionHandler(EnRr* this, PlayState* play) {
    Player* player = GET_PLAYER(play);
    f32 snapRateXZ = this->actor.scale.y * 650.0f; // How quickly player "snaps" to target pos.
    f32 snapRateY = this->actionFunc != EnRr_ScoopPlayer ? this->grabEject > 4 ? 4.0f : this->actor.scale.y * 1200.0f
                                                         : this->actor.scale.y * 900.0f;
    f32 invScaleY = 1.0f / this->actor.scale.y;
    Math_StepToF(&player->actor.world.pos.x, this->bodySphPos[3].x, snapRateXZ);
    Math_StepToF(&player->actor.world.pos.y, this->bodySphPos[3].y + this->swallowOffset,
                 snapRateY); // Rolls off y snapRate if player is stuck on something.
    Math_StepToF(&player->actor.world.pos.z, this->bodySphPos[3].z, snapRateXZ);
    f32 decRate = ((this->heightRef / 30.0f) * (1.0f - (this->actor.scale.y * 15.0f)));
    f32 vacuumMod = this->vacuumCooldown ? 1.35f : 1.0f; // Getting caught during a vacuum speeds up descent.
    f32 offsetTarget = (!TYPE_INVERT(this)) ? -this->heightRef * 0.8f : this->heightRef * 0.05f;
    if (this->actionFunc != EnRr_ScoopPlayer) {
        Math_StepToF(&this->swallowOffset, offsetTarget, decRate * vacuumMod);
    }

    if (this->actor.scale.y <= 0.015f && (LINK_IS_ADULT)) {
        f32 playerScaleTarget = 0.0085f;
        Math_StepToF(&player->actor.scale.x, playerScaleTarget,
                     (0.01f - playerScaleTarget) / (this->transitionRate * 2.0f));
        Math_StepToF(&player->actor.scale.y, playerScaleTarget,
                     (0.01f - playerScaleTarget) / (this->transitionRate * 2.0f));
        Math_StepToF(&player->actor.scale.z, playerScaleTarget,
                     (0.01f - playerScaleTarget) / (this->transitionRate * 2.0f));
    }
}

void EnRr_ScoopPlayer(EnRr* this, PlayState* play) {
    Player* player = GET_PLAYER(play);

    EnRr_GrabPlayerPositionHandler(this, play);

    if (this->phaseCycleCount == 0) {
        EnRr_SetGrabParams(this, player, play);
        this->actionFunc = EnRr_GrabPlayer;
    }
}

// All of the following helper functions below are for GrabPlayer.
void EnRr_GrabStruggleHandler(EnRr* this, PlayState* play) {
    Player* player = GET_PLAYER(play);

    if (player->av2.actionVar2 > 0) {
        if (this->catchPenalty <= 0) {
            this->struggleSpeedup = 20; // Resets this timer.
        }

        if (!Audio_IsSfxPlaying(NA_SE_EV_BUYOSHUTTER_CLOSE) && !Audio_IsSfxPlaying(NA_SE_EV_WATER_BUBBLE) &&
            !Audio_IsSfxPlaying(NA_SE_EN_LIKE_DRINK)) {
            this->struggleCounter += player->av2.actionVar2; // Transfers av2 into counter.
        } else {
            this->struggleCounter++; // Allows the long "item steal" SFX to play out fully.
        }
        if ((player->av2.actionVar2 > 0) && (this->struggleSound == 0) && (this->grabState != 2) &&
            (!TYPE_DRAIN(this)) && !(this->retreat && this->grabState == 1)) {
            Audio_PlaySoundTransposed(&this->actor.projectedPos, NA_SE_EN_TEKU_WALK_WATER, this->pitchScale);
            this->struggleSound = 10;
        }

        player->av2.actionVar2 = 0; // Force player var back to 0.
    }

    DECR(this->struggleSound);
    DECR(this->struggleSpeedup);

    if (this->struggleCounter > 0) {
        if ((this->grabState == 1) && !(this->retreat)) {
            this->struggleCounter -= this->actor.params == LIKE_LIKE_GIANT ? 2 : 1;
        } else if ((this->grabState == 2) && (this->frameCount % 2 != 0)) { // Swapped to frameCount!
            this->struggleCounter -= 3;
        } else if ((this->grabState == 3 || this->retreat) && (this->frameCount % 2 != 0) &&
                   (!Audio_IsSfxPlaying(NA_SE_EN_LIKE_DRINK) && !Audio_IsSfxPlaying(NA_SE_EV_BUYOSHUTTER_CLOSE))) {
            this->struggleCounter--;
        }
        if (this->struggleSpeedup <= 0) {
            this->struggleCounter -= 2;
        }
    }
    if (this->struggleCounter < 0) {
        this->struggleCounter = 0;
    }

    if (!this->playerInside) {
        this->struggleSound = 6;
        this->struggleSpeedup = 0;
    }
}

void EnRr_GrabEjectHandler(EnRr* this, PlayState* play) {
    Player* player = GET_PLAYER(play);
    f32 distXZ = Math_Vec3f_DistXZ(&this->bodySphPos[3], &player->actor.world.pos);
    f32 distY = fabsf((this->bodySphPos[3].y + this->swallowOffset) - player->actor.world.pos.y);
    Vec3f hitPos;
    CollisionPoly* poly;
    s32 bgId;

    // If player is either out of range for too long, they're ejected.
    if ((distXZ > this->bodyRadiusRef) || (distY > this->heightRef) ||
        (BgCheck_EntityLineTest1(&play->colCtx, &player->actor.world.pos, &this->bodySphPos[3], &hitPos, &poly, true,
                                 true, true, true, &bgId))) {
        this->grabEject++;
        if (this->grabEject > 10) {
            EnRr_SetupThrowPlayer(this, play);
        }
    } else {
        this->grabEject = 0;
    }
}

bool EnRr_FindStealableBottle(u8* outSlot, u8* outItem) {
    for (u8 slot = SLOT_BOTTLE_1; slot < SLOT_BOTTLE_4; slot++) {
        u8 item = gSaveContext.inventory.items[slot];
        if ((item >= ITEM_BOTTLE && item <= ITEM_POE) && (item != ITEM_LETTER_RUTO) && (item != ITEM_BLUE_FIRE) &&
            (item != ITEM_BIG_POE)) {
            *outSlot = slot;
            *outItem = item;
            return true;
        }
    }
    return false;
}

bool EnRr_FindStealableItem(u8* outSlot, u8* outItem) {
    static const u8 itemWhitelist[] = {
        SLOT_HOOKSHOT,
        SLOT_BOOMERANG,
        SLOT_LENS,
        SLOT_HAMMER,
    };

    for (u32 i = 0; i < ARRAY_COUNT(itemWhitelist); i++) {
        u8 slot = itemWhitelist[i];
        u8 item = gSaveContext.inventory.items[slot];

        if (item != ITEM_NONE &&
            ((LINK_IS_ADULT && (item == ITEM_HOOKSHOT || item == ITEM_LONGSHOT || item == ITEM_HAMMER)) ||
             (LINK_IS_CHILD && item == ITEM_BOOMERANG) || item == ITEM_LENS)) {
            *outSlot = slot;
            *outItem = item;
            return true;
        }
    }

    return false;
}

void EnRr_AddStolenShield(u8 itemId) {
    if (gSaveContext.inventory.eatenShield == EQUIP_VALUE_SHIELD_MIRROR) {
        gSaveContext.inventory.eatenShield = itemId;
    }
    return;
}

void EnRr_AddStolenSword(u8 itemId) {
    s16 i;
    for (i = 0; i < ARRAY_COUNT(gSaveContext.inventory.eatenSwords); i++) {
        if (gSaveContext.inventory.eatenSwords[i] != EQUIP_VALUE_SWORD_NONE) {
            gSaveContext.inventory.eatenSwords[i] = itemId;
            return;
        }
    }
}

void EnRr_AddStolenBottle(u8 itemId) {
    s16 i;
    for (i = 0; i < ARRAY_COUNT(gSaveContext.inventory.eatenBottles); i++) {
        if (gSaveContext.inventory.eatenBottles[i] != ITEM_BOTTLE) {
            gSaveContext.inventory.eatenBottles[i] = ITEM_BOTTLE;
            break;
        }
    }
}

void EnRr_AddStolenItem(u8 itemId) {
    s16 i;
    for (i = 0; i < ARRAY_COUNT(gSaveContext.inventory.eatenItems); i++) {
        if (gSaveContext.inventory.eatenItems[i] == 0) {
            gSaveContext.inventory.eatenItems[i] = itemId;
            return;
        }
    }
}

void EnRr_GrabPlayer(EnRr* this, PlayState* play) {
    Player* player = GET_PLAYER(play);
    EnRrStruct* mouthSegment = &this->bodySegs[this->bodySegCount];
    s16 soundMod;
    f32 phaseVelMod;
    f32 wobbleMod;
    f32 pulseMod;
    u8 bottleSlot;
    u8 bottleId;
    u8 itemSlot;
    u8 itemId;

    func_800AA000(this->actor.xyzDistToPlayerSq, 120, 2, 120);

    this->regrabTimer = 8;
    player->actor.speedXZ = 0.0f;
    player->actor.velocity.y = this->actor.velocity.y;

    EnRr_GrabStruggleHandler(this, play);

    // Determines if player is fully inside Like Like, allows struggleCounter to increment and phaseCycleCounter to
    // decrement.
    if ((((!TYPE_INVERT(this)) && (this->swallowOffset < -player->cylinder.dim.height * 0.85f)) ||
         ((TYPE_INVERT(this)) && (this->swallowOffset == this->heightRef * 0.05f && mouthSegment->height == 0.0f))) &&
        (!this->playerInside)) {
        this->playerInside = true;
        if (this->retreat || (TYPE_DRAIN(this))) {
            this->phaseCycleTimer = 0;
            this->phaseCycleCount = 24;
        }
    }

    EnRr_GrabEjectHandler(this, play);

    // Calculate the phase crossing locally
    s16 oldPhase = (s16)(this->segMovePhase - this->segPhaseVel);
    s16 currentPhase = (s16)this->segMovePhase;

    // We still need to know how many frames a half-cycle takes so we can set the Color Filter duration!
    s16 halfCycleFrames = 0x8000 / this->segPhaseVel;

    // All grab effects are synchronized perfectly with the visual geometry!
    if ((oldPhase ^ currentPhase) < 0) {
        if (this->playerInside) {
            // Item-stealing types deal damage during the steal phase or if the player is grabbed again while it's
            // retreating
            if ((((this->actor.params == LIKE_LIKE_SMALL && this->phaseCycleCount % 8 == 0) ||
                  ((this->actor.params == LIKE_LIKE_NORMAL || this->actor.params == LIKE_LIKE_STATIONARY ||
                    this->actor.params == LIKE_LIKE_INVERT || this->actor.params == LIKE_LIKE_STATIONARY_INVERT) &&
                   this->phaseCycleCount % 6 == 0) ||
                  (this->actor.params == LIKE_LIKE_GIANT && this->phaseCycleCount % 4 == 0)) &&
                 (this->grabState == 2 && gSaveContext.health > 16)) ||
                (this->retreat && this->grabState == 1 && this->phaseCycleCount % 3 == 0) ||
                (this->actor.params == LIKE_LIKE_SMALL && LINK_IS_ADULT && this->phaseCycleCount % 4 == 0)) {
                play->damagePlayer(play, -2);
                this->slimeCounter += 10;
                if (this->retreat) {
                    CollisionCheck_SpawnWaterDroplets(play, &this->bodySphPos[3]);
                }
                if (this->actor.params == LIKE_LIKE_SMALL && LINK_IS_ADULT) {
                    Audio_PlayActorSound2(&player->actor, NA_SE_VO_LI_DAMAGE_S + player->ageProperties->unk_92);
                }
            }
            // For every 3 phaseCycleCount decrements, life is stolen; max life is capped out at 12
            if (this->actor.params == LIFE_LIKE) {
                this->stolenLife++;
                if (this->stolenLife == 4) {
                    if (this->maximumHealth < 12 && this->actor.colChkInfo.health == this->maximumHealth) {
                        this->maximumHealth++;
                    }
                    if (this->actor.colChkInfo.health < this->maximumHealth) {
                        this->actor.colChkInfo.health++;
                    }
                    this->stolenLife = 0;
                    this->slimeCounter += 5;
                    play->damagePlayer(play, -8);
                    Audio_PlayActorSound2(&this->actor, NA_SE_SY_HP_RECOVER);
                    Actor_SetColorFilter(&this->actor, 0x4000, 98, 0, soundMod * 2);
                }
            }

            if (TYPE_DRAIN(this) && this->phaseCycleCount < 3) {
                this->phaseCycleCount += 20; // Effectively freezes the cycle countdown; player must break free.
            }

            this->slimeCounter += 10;
            this->soundEatCounter++;
            DECR(this->catchPenalty);
            if (this->throwStrength < 5 && this->soundEatCounter % 3 == 0) {
                this->throwStrength++;
            }

            if ((this->grabState == 2 && this->phaseCycleCount < 96) || (TYPE_DRAIN(this))) {
                if (this->phaseCycleCount % 2 == 0) {
                    Audio_PlaySoundTransposed(&this->actor.projectedPos, NA_SE_SY_GLASSMODE_ON, this->pitchScale);
                    if (this->actor.params == MAGIC_LIKE) {
                        if (gSaveContext.magic != 0 && !(Flags_GetRandomizerInf(RAND_INF_HAS_INFINITE_MAGIC_METER))) {
                            gSaveContext.magic -= 1;
                            gSaveContext.magicTarget = gSaveContext.magic;
                            // Uses halfCycleFrames!
                            Actor_SetColorFilter(&this->actor, 0x0000, 98, 0, halfCycleFrames * 2);
                        }
                    }
                }
            }
        }
        if (this->retreat && this->grabState == 1) {
            Audio_PlaySoundTransposed(&this->actor.projectedPos, NA_SE_EN_TEKU_WALK_WATER, this->pitchScale);
        }

        Audio_PlaySoundTransposed(&this->actor.projectedPos, NA_SE_EN_LIKE_EAT, this->pitchScale);
    }

    if ((this->playerInside) && (this->actor.params == RUPEE_LIKE) && (gSaveContext.rupees > 0) &&
        !(Flags_GetRandomizerInf(RAND_INF_HAS_INFINITE_MONEY))) {
        s16 phaseRupeeTarget;
        phaseRupeeTarget = CLAMP_MIN(0x6000 - (gSaveContext.rupees * 0x20), 0x2000);
        this->phaseRupeeTimer += this->segPhaseVel;
        if (this->phaseRupeeTimer > phaseRupeeTarget) { // Steal 1 rupee every quarter-cycle.
            this->phaseRupeeTimer -= phaseRupeeTarget;
            Rupees_ChangeBy(-1);
            this->eatenRupees++;
            this->slimeCounter += 2;
        }
    }

    switch (this->grabState) {
        case 1:
            // Mod parameters react to player struggling.
            phaseVelMod = 1024.0f * ((f32)this->struggleCounter / BREAKFREE_TARGET);
            wobbleMod = 1024.0f * ((f32)this->struggleCounter / BREAKFREE_TARGET);
            pulseMod = 0.025f * ((f32)this->struggleCounter / BREAKFREE_TARGET);

            if (this->actor.params == LIKE_LIKE_SMALL && LINK_IS_ADULT) {
                this->segPhaseVelTarget = 4096;
                this->wobbleSizeTarget = 512.0f + wobbleMod * 1.5f; // Caps at 1792
                mouthSegment->scaleTarget = 0.925f;
                this->innerMouthScaleTarget = 1.25f;
            } else if (!this->retreat && (!TYPE_DRAIN(this))) {
                // Item-stealing types slowly close mouth to indicate how close they are to stealing.
                this->segPhaseVelTarget = 4096 - phaseVelMod; // Caps at 3072
                mouthSegment->scaleTarget =
                    0.85f - (8.0f - (f32)this->phaseCycleCount / 2.0f) / 80.0f - pulseMod * 3.0f;
                this->innerMouthScaleTarget = 0.85f - (8.0f - (f32)this->phaseCycleCount / 2.0f) / 10.0f;
                this->wobbleSizeTarget = 768.0f + wobbleMod; // Caps at 1280
            } else {
                this->segPhaseVelTarget = 4096 - phaseVelMod * 1.5f; // Caps at 1536
                this->wobbleSizeTarget = 512.0f + wobbleMod * 1.5f;  // Caps at 1792
                mouthSegment->scaleTarget = 0.8f - pulseMod * 3.0f;
                this->innerMouthScaleTarget = 0.625f - pulseMod * 3.0f;
            }
            this->pulseSizeTarget = PULSE_SIZE_DEFAULT + pulseMod; // Caps at 0.175
            this->segPhaseVelRate = 64;

            if (this->phaseCycleCount == 0) {
                if (this->retreat || (this->actor.params == LIKE_LIKE_SMALL && LINK_IS_ADULT)) {
                    EnRr_SetupThrowPlayer(this, play);
                } else if (((this->eatenShield == 0 && CUR_EQUIP_VALUE(EQUIP_TYPE_SHIELD) != EQUIP_VALUE_SHIELD_NONE) ||
                            (this->eatenSword == 0 && CUR_EQUIP_VALUE(EQUIP_TYPE_SWORD) != EQUIP_VALUE_SWORD_NONE) ||
                            (this->eatenTunic == 0 && CUR_EQUIP_VALUE(EQUIP_TYPE_TUNIC) != EQUIP_VALUE_TUNIC_KOKIRI)) ||
                           //(this->eatenBoots == 0 && CUR_EQUIP_VALUE(EQUIP_TYPE_BOOTS) != EQUIP_VALUE_BOOTS_NONE)) ||
                           (this->eatenBottle == 0 && EnRr_FindStealableBottle(&bottleSlot, &bottleId)) ||
                           (this->eatenItem == 0 && EnRr_FindStealableItem(&itemSlot, &itemId))) {
                    // Go to stealing phase if there's something to steal
                    this->transitionRate = 30.0f;
                    mouthSegment->scaleTarget = 0.75f;
                    this->innerMouthScaleTarget = 0.2f;
                    this->scaleRate2 = mouthSegment->scaleTarget / this->transitionRate;
                    this->segPhaseVelTarget = 5461;
                    this->segPhaseVelRate = abs(this->segPhaseVelTarget - this->segPhaseVel) / this->transitionRate;
                    this->wobbleSizeTarget = this->actor.params == LIKE_LIKE_GIANT ? 256.0f : 1024.0f;
                    this->wobbleSizeRate = fabsf(this->wobbleSizeTarget - this->wobbleSize) / this->transitionRate;
                    this->pulseSizeTarget = PULSE_SIZE_DEFAULT;
                    this->pulseSizeRate = fabsf(this->pulseSize - this->pulseSizeTarget) / this->transitionRate;
                    this->segWobbleXTarget = 4.0f;
                    this->segWobbleXRate = (this->segWobbleXTarget - this->segWobblePhaseDiffX) / this->transitionRate;
                    this->segWobbleZTarget = 4.0f;
                    this->segWobbleZRate = (this->segWobbleZTarget - this->segWobblePhaseDiffZ) / this->transitionRate;
                    this->segScaleModYTarget = 0.03f;
                    this->phaseCycleTimer = 0;
                    this->phaseCycleCount = 96;
                    this->soundEatCounter = 0;
                    this->catchPenalty = this->actor.params == LIKE_LIKE_GIANT ? 12 : 8;
                    this->struggleCounter = 0; // Resets struggle counter, catch penalty ensures player will take some
                                               // damage before breaking from steal phase.
                    Audio_PlaySoundTransposed(&this->actor.projectedPos, NA_SE_EV_BUYOSHUTTER_CLOSE, this->pitchScale);
                    // Audio_PlaySoundTransposed(&this->actor.projectedPos, NA_SE_EV_BUYODOOR_CLOSE, this->pitchScale);
                    Audio_PlaySoundTransposed(&this->actor.projectedPos, NA_SE_EN_LIKE_UNARI, this->pitchScale);
                    this->grabState = 2;
                } else {
                    // Otherwise, go to idle grab state
                    this->scaleRate2 = 0.05f;
                    this->segPhaseVelRate = 64;
                    this->segWobbleXRate = 0.1f;
                    this->segWobbleZRate = 0.07f;
                    this->wobbleSizeRate = 64.0f;
                    this->segScaleModYTarget = SCALE_MOD_Y_DEFAULT;
                    this->phaseCycleTimer = 0;
                    this->phaseCycleCount = 32;
                    this->struggleCounter -= BREAKFREE_TARGET / 5;
                    mouthSegment->scaleTarget = 0.85f;
                    this->innerMouthScaleTarget = 0.75f;
                    this->scaleRate2 =
                        (mouthSegment->scaleTarget - mouthSegment->scale) / (this->transitionRate * 0.5f);
                    play->damagePlayer(play, -8);
                    Audio_PlaySoundTransposed(&this->actor.projectedPos, NA_SE_EV_BUYOSHUTTER_OPEN, this->pitchScale);
                    Audio_PlaySoundTransposed(&this->actor.projectedPos, NA_SE_EV_BUYODOOR_OPEN, this->pitchScale);
                    this->slimeCounter += 10;
                    this->grabState = 3;
                }
            }
            break;
        case 2:
            pulseMod = 0.04f * ((f32)this->struggleCounter / BREAKFREE_TARGET);
            f32 mouthSegMod = 0.35f * ((f32)this->struggleCounter / BREAKFREE_TARGET);
            this->pulseSizeTarget = PULSE_SIZE_DEFAULT + pulseMod;
            mouthSegment->scaleTarget = 0.75f - mouthSegMod;
            this->innerMouthScaleTarget = 0.2f + mouthSegMod;

            if (this->phaseCycleCount == 0 || this->struggleCounter > BREAKFREE_TARGET >> 1) {
                if (this->eatenShield == 0 && CUR_EQUIP_VALUE(EQUIP_TYPE_SHIELD) != EQUIP_VALUE_SHIELD_NONE) {
                    this->eatenShield = CUR_EQUIP_VALUE(EQUIP_TYPE_SHIELD);
                    this->heldItem = EQUIP_TYPE_SHIELD;
                    EnRr_AddStolenShield(this->eatenShield);
                    this->msgEaten = Inventory_DeleteEquipment(play, EQUIP_TYPE_SHIELD);
                } else if (this->eatenSword == 0 && CUR_EQUIP_VALUE(EQUIP_TYPE_SWORD) != EQUIP_VALUE_SWORD_MASTER &&
                           CUR_EQUIP_VALUE(EQUIP_TYPE_SWORD) != EQUIP_VALUE_SWORD_NONE) {
                    this->eatenSword = CUR_EQUIP_VALUE(EQUIP_TYPE_SWORD);
                    this->heldItem = EQUIP_TYPE_SWORD;
                    EnRr_AddStolenSword(this->eatenSword);
                    this->msgEaten = Inventory_DeleteEquipment(play, EQUIP_TYPE_SWORD);
                } else if (this->eatenTunic == 0 && CUR_EQUIP_VALUE(EQUIP_TYPE_TUNIC) != EQUIP_VALUE_TUNIC_KOKIRI &&
                           CUR_EQUIP_VALUE(EQUIP_TYPE_TUNIC) != EQUIP_VALUE_TUNIC_NONE) {
                    this->eatenTunic = CUR_EQUIP_VALUE(EQUIP_TYPE_TUNIC);
                    this->heldItem = EQUIP_TYPE_TUNIC;
                    this->msgEaten = this->heldItem = Inventory_DeleteEquipment(play, EQUIP_TYPE_TUNIC);
                    //} else if (this->eatenBoots == 0 && CUR_EQUIP_VALUE(EQUIP_TYPE_BOOTS) != EQUIP_VALUE_BOOTS_NONE) {
                    //    this->eatenBoots = CUR_EQUIP_VALUE(EQUIP_TYPE_BOOTS);
                    //    this->heldItem = EQUIP_TYPE_BOOTS;
                    //    this->msgEaten = Inventory_DeleteEquipment(play, EQUIP_TYPE_BOOTS);
                } else if (this->eatenBottle == 0 && EnRr_FindStealableBottle(&bottleSlot, &bottleId)) {
                    this->eatenBottle = gSaveContext.inventory.items[bottleSlot];
                    this->msgEaten = this->heldItem = 4;
                    EnRr_AddStolenBottle(this->eatenBottle);
                    Inventory_DeleteItem(bottleId, bottleSlot);
                } else if (this->eatenItem == 0 && EnRr_FindStealableItem(&itemSlot, &itemId)) {
                    this->eatenItem = gSaveContext.inventory.items[itemSlot];
                    EnRr_AddStolenItem(this->eatenItem);
                    this->msgEaten = this->heldItem = 5;
                    Inventory_DeleteItem(itemId, itemSlot);
                }

                if (this->msgEaten != -1) {
                    play->damagePlayer(play, -8);
                    this->retreat = true;
                    Audio_StopSfxById(NA_SE_EN_BIRI_BUBLE);
                    Audio_PlaySoundTransposed(&this->actor.projectedPos, NA_SE_EN_LIKE_DRINK, this->pitchScale);
                    Audio_PlaySoundTransposed(&this->actor.projectedPos, NA_SE_EV_WATER_BUBBLE, this->pitchScale);
                }

                this->phaseCycleTimer = 0;
                this->phaseCycleCount = 32;
                this->struggleCounter >>= 1;
                this->segPhaseVelRate = 64;
                this->segWobbleXRate = 0.05f;
                this->segWobbleZRate = 0.015f;
                this->wobbleSizeRate = 64.0f;
                this->segScaleModYTarget = SCALE_MOD_Y_DEFAULT;
                mouthSegment->scaleTarget = 0.85f;
                this->innerMouthScaleTarget = 0.75f;
                this->scaleRate2 = ABS(mouthSegment->scaleTarget - mouthSegment->scale) / (this->transitionRate * 0.5f);
                this->grabState = 3;
            }
            break;
        case 3:
            phaseVelMod = 1920.0f * ((f32)this->struggleCounter / BREAKFREE_TARGET);
            wobbleMod = 1920.0f * ((f32)this->struggleCounter / BREAKFREE_TARGET);
            pulseMod = 0.04f * ((f32)this->struggleCounter / BREAKFREE_TARGET);
            f32 wobbleDiffMod = 0.75f * ((f32)this->struggleCounter / BREAKFREE_TARGET);
            this->segPhaseVelTarget = 2978 - phaseVelMod;
            this->wobbleSizeTarget = 256.0f + wobbleMod;
            mouthSegment->scaleTarget = 0.85f - pulseMod * 4.375f;
            this->innerMouthScaleTarget = 0.75f - pulseMod * 4.375f;
            this->pulseSizeTarget = PULSE_SIZE_DEFAULT + pulseMod;
            this->segWobbleXTarget = WOBBLE_DIFF_X_DEFAULT - wobbleDiffMod;
            this->segWobbleZTarget = WOBBLE_DIFF_Z_DEFAULT + wobbleDiffMod * 0.625f;

            if (this->phaseCycleCount == 0) {
                EnRr_SetupThrowPlayer(this, play); // Failsafe, won't decrement in this state.
            }
            break;
    }

    if (this->struggleCounter > BREAKFREE_TARGET || !(player->stateFlags2 & PLAYER_STATE2_GRABBED_BY_ENEMY)) {
        this->grabState = 0;
        this->playerInside = false;
        EnRr_SetupThrowPlayer(this, play);
    } else {
        EnRr_GrabPlayerPositionHandler(this, play);
    }
}

void EnRr_ThrowPlayer(EnRr* this, PlayState* play) {
    EnRrStruct* segment;
    s16 i;
    segment = &this->bodySegs[this->bodySegCount - 2];
    f32 lerpMod = segment->height / segment->heightTarget;
    Player* player = GET_PLAYER(play);
    player->av2.actionVar2 = 0;
    player->actor.speedXZ = 0.0f;
    player->actor.velocity.y = this->actor.velocity.y;

    this->regrabTimer = 8;

    // Roll player in sync with Like Like.
    s32 rotXSum = 0;
    for (i = 1; i < this->bodySegCount; i++) {
        segment = &this->bodySegs[i];
        rotXSum += segment->rot.x;
    }

    s32 targetRotX = this->storedPlayerIsFacing ? -rotXSum : rotXSum;
    s32 finalPlayerRotX = targetRotX * 1.75f;

    player->actor.shape.rot.x = F32_LERPIMP(0, finalPlayerRotX, lerpMod * 0.75f);
    player->actor.world.rot.x = player->actor.shape.rot.x;
    player->actor.shape.rot.y = this->storedPlayerIsFacing ? this->actor.world.rot.y - 0x8000 : this->actor.world.rot.y;

    // LERP player to mouth based on height / heightTarget.
    player->actor.world.pos.x = F32_LERPIMP(this->actor.world.pos.x, this->bodySphPos[3].x, lerpMod * 0.8f);
    player->actor.world.pos.y =
        F32_LERPIMP(this->actor.world.pos.y, this->bodySphPos[3].y - player->cylinder.dim.height * 0.5f, lerpMod);
    player->actor.world.pos.z = F32_LERPIMP(this->actor.world.pos.z, this->bodySphPos[3].z, lerpMod * 0.8f);

    if (this->actor.scale.y <= 0.015f && (LINK_IS_ADULT)) {
        f32 playerScaleTarget = 0.01f;
        Math_StepToF(&player->actor.scale.x, playerScaleTarget, playerScaleTarget / (this->transitionRate * 0.5f));
        Math_StepToF(&player->actor.scale.y, playerScaleTarget, playerScaleTarget / (this->transitionRate * 0.5f));
        Math_StepToF(&player->actor.scale.z, playerScaleTarget, playerScaleTarget / (this->transitionRate * 0.5f));
    }

    // Function will end once the segment below mouth reaches height target.
    if (segment->height == segment->heightTarget) {
        this->reachState = 0;
        EnRr_SetupReleasePlayer(this, play);
        if (this->damageRelease == 0) {
            EnRr_SetupNeutral(this, play);
        } else {
            this->damageRelease = 0;
            EnRr_SetupDamage(this);
        }
    }
}

void EnRr_Damage(EnRr* this, PlayState* play) {
    EnRrStruct* segment;
    s16 i;
    s16 damageWobbleTarget;
    s16 damageInvincDiv;
    s16 damageWobbleDir;

    if (this->invincibilityTimer <= 10) {
        EnRr_SetupApproach(this);
        return;
    }

    damageWobbleTarget = this->actor.colChkInfo.damageEffect == RR_DMG_HAMMER ? 1000 : 4000;
    damageInvincDiv = this->actor.colChkInfo.damageTable == RR_DMG_HAMMER ? 2 : 8;
    damageWobbleDir = this->invincibilityTimer & damageInvincDiv ? damageWobbleTarget : -damageWobbleTarget;

    for (i = 1; i <= this->bodySegCount; i++) {
        segment = &this->bodySegs[i];
        segment->rotTargetZ = damageWobbleDir;
    }
}

extern GetItemEntry CBridge_GetItemEntryFromRG(int rgId);

void EnRr_DropStolenItem(PlayState* play, Vec3f* spawnPos, s32 rgId) {
    // 1. Drop the _GI suffix here! Use the silent pickup parameter.
    EnItem00* drop = (EnItem00*)Actor_Spawn(&play->actorCtx, play, ACTOR_EN_ITEM00, 
                                            spawnPos->x, spawnPos->y, spawnPos->z, 
                                            0, 0, 0, 
                                            ITEM00_SOH_GIVE_ITEM_ENTRY, true);

    if (drop != NULL) {
        // 2. REMOVE the drop->getItemId assignment completely!
        
        // Fetch the struct through the bridge
        drop->itemEntry = CBridge_GetItemEntryFromRG(rgId); 
        
        // Apply bouncy drop physics
        drop->actor.velocity.y = 8.0f;
        drop->actor.speedXZ = 2.0f;
        drop->actor.gravity = -0.9f;
        drop->actor.world.rot.y = Rand_CenteredFloat(65536.0f);
        
        drop->actor.flags |= ACTOR_FLAG_UPDATE_CULLING_DISABLED;
        drop->unk_15A = 220; 
    }
}

void EnRr_Death(EnRr* this, PlayState* play) {
    EnRrStruct* segment;
    s16 i;
    f32 targetScale;

    this->actor.colorFilterTimer = 40;
    if (this->frameCount < 40) {
        for (i = 0; i <= this->bodySegCount; i++) {
            segment = &this->bodySegs[i];
            Math_StepToF(&segment->heightTarget, (i + 59) - (this->frameCount * 25.0f), 50.0f);
            segment->scaleTarget = (SQ((f32)(4 - i)) * this->frameCount * 0.003f) + 1.0f;
        }
        return;
    }

    if (this->frameCount >= 95) {
        // 1. Collect all stolen items into an array
        s32 itemsToDrop[6]; // Max possible stolen items at once
        u8 dropCount = 0;

        // Shields
        if (this->eatenShield == 1) itemsToDrop[dropCount++] = RG_DEKU_SHIELD;
        else if (this->eatenShield == 2) itemsToDrop[dropCount++] = RG_HYLIAN_SHIELD;
        else if (this->eatenShield == 3) itemsToDrop[dropCount++] = RG_MIRROR_SHIELD;

        // Tunics
        // if (this->eatenTunic == 1) itemsToDrop[dropCount++] = RG_KOKIRI_TUNIC;
        if (this->eatenTunic == 2) itemsToDrop[dropCount++] = RG_GORON_TUNIC;
        else if (this->eatenTunic == 3) itemsToDrop[dropCount++] = RG_ZORA_TUNIC;

        // Boots
        //if (this->eatenBoots == 1) itemsToDrop[dropCount++] = RG_KOKIRI_BOOTS;
        if (this->eatenBoots == 2) itemsToDrop[dropCount++] = RG_IRON_BOOTS;
        else if (this->eatenBoots == 3) itemsToDrop[dropCount++] = RG_HOVER_BOOTS;

        // Swords
        if (this->eatenSword == 1) itemsToDrop[dropCount++] = RG_KOKIRI_SWORD;
        else if (this->eatenSword == 2) itemsToDrop[dropCount++] = RG_MASTER_SWORD;
        else if (this->eatenSword == 3) itemsToDrop[dropCount++] = RG_BIGGORON_SWORD;

        // Bottles
        switch (this->eatenBottle) {
            case 20: itemsToDrop[dropCount++] = RG_EMPTY_BOTTLE; break;
            case 21: itemsToDrop[dropCount++] = RG_BOTTLE_WITH_RED_POTION; break;
            case 22: itemsToDrop[dropCount++] = RG_BOTTLE_WITH_GREEN_POTION; break;
            case 23: itemsToDrop[dropCount++] = RG_BOTTLE_WITH_BLUE_POTION; break;
            case 24: itemsToDrop[dropCount++] = RG_BOTTLE_WITH_FAIRY; break;
            case 25: itemsToDrop[dropCount++] = RG_BOTTLE_WITH_FISH; break;
            case 26: itemsToDrop[dropCount++] = RG_BOTTLE_WITH_MILK; break;
            case 29: itemsToDrop[dropCount++] = RG_BOTTLE_WITH_BUGS; break;
            case 32: itemsToDrop[dropCount++] = RG_BOTTLE_WITH_POE; break;
        }

        // Equipment/Items
        switch (this->eatenItem) {
            case 10: itemsToDrop[dropCount++] = RG_HOOKSHOT; break;
            case 11: itemsToDrop[dropCount++] = RG_LONGSHOT; break;
            case 14: itemsToDrop[dropCount++] = RG_BOOMERANG; break;
            case 15: itemsToDrop[dropCount++] = RG_LENS_OF_TRUTH; break;
            case 17: itemsToDrop[dropCount++] = RG_MEGATON_HAMMER; break;
        }

        // 2. Spawn the stolen items with a radial burst offset
        for (int j = 0; j < dropCount; j++) {
            Vec3f spawnPos = this->actor.world.pos;
            
            if (dropCount > 1) {
                s16 angle = (s16)(j * (65536.0f / dropCount));
                spawnPos.x += Math_SinS(angle) * 15.0f; 
                spawnPos.z += Math_CosS(angle) * 15.0f;
            }

            EnRr_DropStolenItem(play, &spawnPos, itemsToDrop[j]);
        }

        // 3. Keep vanilla drops tied to the Like Like's parameter variant
        switch (this->actor.params) {
            case LIKE_LIKE_NORMAL:
            case LIKE_LIKE_STATIONARY:
            case LIKE_LIKE_INVERT:
            case LIKE_LIKE_STATIONARY_INVERT:
                Item_DropCollectible(play, &this->actor.world.pos, ITEM00_RUPEE_BLUE);
                Item_DropCollectible(play, &this->actor.world.pos, ITEM00_RUPEE_BLUE);
                Item_DropCollectible(play, &this->actor.world.pos, ITEM00_RUPEE_BLUE);
                break;
            case LIKE_LIKE_SMALL:
                Item_DropCollectible(play, &this->actor.world.pos, ITEM00_RUPEE_BLUE);
                break;
            case LIKE_LIKE_GIANT:
                Item_DropCollectible(play, &this->actor.world.pos, ITEM00_RUPEE_BLUE);
                Item_DropCollectible(play, &this->actor.world.pos, ITEM00_RUPEE_BLUE);
                Item_DropCollectible(play, &this->actor.world.pos, ITEM00_RUPEE_RED);
                break;
            case RUPEE_LIKE:
                Item_DropCollectible(play, &this->actor.world.pos, ITEM00_RUPEE_RED);
                Item_DropCollectible(play, &this->actor.world.pos, ITEM00_RUPEE_RED);
                break;
            case LIFE_LIKE:
                Item_DropCollectible(play, &this->actor.world.pos, ITEM00_HEART);
                break;
            case MAGIC_LIKE:
                Item_DropCollectible(play, &this->actor.world.pos, ITEM00_MAGIC_SMALL);
                break;
        }

        Actor_Kill(&this->actor);
        return;
    }

    if (this->frameCount == 88) {
        Vec3f pos;
        Vec3f vel = { 0.0f, 0.0f, 0.0f };
        Vec3f accel = { 0.0f, 0.0f, 0.0f };

        pos.x = this->actor.world.pos.x;
        pos.y = this->actor.world.pos.y + 20.0f;
        pos.z = this->actor.world.pos.z;

        EffectSsDeadDb_Spawn(play, &pos, &vel, &accel, 100, 0, 255, 255, 255, 255, 255, 0, 0, 1, 9, true);
        SoundSource_PlaySfxAtFixedWorldPos(play, &pos, 11, NA_SE_EN_EXTINCT);
    } else {
        targetScale = this->actor.scale.y * 66.66667f;

        Math_StepToF(&this->actor.scale.x, 0.0f, this->shrinkRate);
        Math_StepToF(&this->shrinkRate, 0.001f * targetScale, 0.00001f * targetScale);
        this->actor.scale.z = this->actor.scale.x;
    }
}

void EnRr_Retreat(EnRr* this, PlayState* play) {
    if (this->phaseCycleCount == 0) {
        this->retreat = false;
        this->segPhaseVelTarget = SEG_PHASE_VEL_DEFAULT;
        if (this->heldItem == 0 && this->eatenSword == EQUIP_VALUE_SWORD_BIGGORON &&
            (!gSaveContext.bgsFlag && (gSaveContext.swordHealth > 0))) {
            gSaveContext.swordHealth = 0;
        }
        u8 healthBoost = this->heldItem == 1 ? 2 : 1;
        this->heldItem = -1;
        this->maximumHealth += healthBoost;
        CLAMP_MAX(this->maximumHealth, 12);
        this->actor.colChkInfo.health += healthBoost; // Gains health after finishing retreat action
        CLAMP_MAX(this->actor.colChkInfo.health, this->maximumHealth);
        if (this->eatenBottle >= ITEM_POTION_RED && this->eatenBottle <= ITEM_POE) {
            this->actor.colChkInfo.health = this->maximumHealth;
            this->eatenBottle = ITEM_BOTTLE;
            Actor_SetColorFilter(&this->actor, 0x4000, 176, 0, 20);
        }
        this->actionFunc = EnRr_Approach;
        Audio_PlaySoundTransposed(&this->actor.projectedPos, NA_SE_EN_LIKE_EAT, this->pitchScale);
    } else {
        // Turn away from the player and move faster during retreat.
        Math_SmoothStepToS(&this->actor.shape.rot.y, BINANG_ROT180(this->actor.yawTowardsPlayer), 0xA, 0x500, 0);
        this->actor.world.rot.y = this->actor.shape.rot.y;
        this->segPhaseVelTarget = 3276;
        this->segScaleModYTarget = 0.0875f;
        if (this->actor.speedXZ == 0.0f && (!TYPE_STATIONARY(this))) {
            EnRr_SetSpeed(this, 3.0f);
            if (!Audio_IsSfxPlaying(NA_SE_EN_AWA_BREAK)) {
                Audio_PlayActorSound2(&this->actor, NA_SE_EN_OCTAROCK_BUBLE);
            }
        }
    }
}

void EnRr_Stunned(EnRr* this, PlayState* play) {
    DECR(this->stunTimer);
    if (this->stunTimer == 0) {
        EnRr_SetupApproach(this);
        this->actionFunc = EnRr_Approach;
    } else if ((this->actor.colChkInfo.health == 0) && (this->stunTimer == 77)) {
        EnRr_SetupDeath(this);
    }
}

void EnRr_GenerateWaterEffects(EnRr* this, PlayState* play) {
    Vec3f pos;

    if ((this->actor.yDistToWater < this->heightRef) && (this->actor.yDistToWater > 1.0f) &&
        ((play->gameplayFrames % 9) == 0)) {
        pos.x = this->actor.world.pos.x;
        pos.y = this->actor.world.pos.y + this->actor.yDistToWater;
        pos.z = this->actor.world.pos.z;
        EffectSsGRipple_Spawn(play, &pos, this->actor.scale.x * 34210.527f, this->actor.scale.x * 60526.316f, 0);
    }

    bool bubbleCondition = (this->actionFunc == EnRr_UnderwaterVacuum ||
                            (this->actionFunc == EnRr_GrabPlayer && this->grabState == 2 && (!TYPE_INVERT(this))));
    if (this->actor.yDistToWater > this->heightRef) {
        if (this->frameCount % 5 == 0 && (!bubbleCondition)) {
            EffectSsBubble_Spawn(play, &this->actor.world.pos, this->heightRef * 0.8f, 0.0f,
                                 this->cylinder.dim.radius * 0.75f, 0.13f);
        } else if (this->frameCount % 3 == 0 && bubbleCondition) {
            f32 bubbleYOffset =
                (!TYPE_INVERT(this)) ? this->heightRef * 0.8f : this->actor.floorHeight - this->actor.world.pos.y;
            EffectSsBubble_Spawn(play, &this->actor.world.pos, bubbleYOffset, 0.0f, this->cylinder.dim.radius * 0.75f,
                                 0.24f);
        }
    }
}

void EnRr_UpdateStepToTargets(EnRr* this, PlayState* play) {
    EnRrStruct* bodySegment;
    EnRrStruct* mouthSegment = &this->bodySegs[this->bodySegCount];
    s16 i;
    Math_ScaledStepToS(&this->segPhaseVel, this->segPhaseVelTarget, this->segPhaseVelRate);
    Math_StepToF(&this->segWobblePhaseDiffX, this->segWobbleXTarget, this->segWobbleXRate);
    Math_StepToF(&this->segWobblePhaseDiffZ, this->segWobbleZTarget, this->segWobbleZRate);
    Math_StepToF(&this->pulseSize, this->pulseSizeTarget, this->pulseSizeRate);
    Math_StepToF(&this->wobbleSize, this->wobbleSizeTarget, this->wobbleSizeRate);
    Math_StepToF(&this->segScaleModY, this->segScaleModYTarget, 0.00125f);

    for (i = 1; i <= this->bodySegCount; i++) {
        bodySegment = &this->bodySegs[i];
        Math_SmoothStepToS(&bodySegment->rot.x, bodySegment->rotTargetX, 5, this->segMoveRate * this->rotXRate, 0);
        Math_SmoothStepToS(&bodySegment->rot.z, bodySegment->rotTargetZ, 5, this->segMoveRate * this->rotZRate, 0);
        Math_StepToF(&bodySegment->height, bodySegment->heightTarget, this->segMoveRate * this->heightRate);
    }

    for (i = 0; i <= this->bodySegCount - 1; i++) {
        bodySegment = &this->bodySegs[i];
        Math_StepToF(&bodySegment->scale, bodySegment->scaleTarget, this->segMoveRate * this->scaleRate1);
    }
    Math_StepToF(&mouthSegment->scale, mouthSegment->scaleTarget, this->segMoveRate * this->scaleRate2);
    Math_SmoothStepToF(&this->innerMouthScale, this->innerMouthScaleTarget, 0.15f, 1.0f, 0.001f);
    Math_StepToF(&this->segMoveRate, 1.0f, 0.2f);
}

void EnRr_UpdateColliderHandler(EnRr* this, PlayState* play) {
    EnRrStruct* segment;
    s16 i;
    ColliderCylinder* bodyCyl = &this->cylinder;
    ColliderJntSphElement* bodySph0 = &this->bodySph.elements[0];
    ColliderJntSphElement* bodySph1 = &this->bodySph.elements[1];
    ColliderJntSphElement* neckSph = &this->bodySph.elements[2];
    ColliderJntSphElement* mouthSph = &this->bodySph.elements[3];
    Vec3f mouthPos = this->bodySphPos[3];

    if (this->reachUp) {
        mouthSph->dim.worldSphere.radius =
            this->actionFunc == EnRr_Reach ? (this->mouthRadiusRef * 1.2f) * this->bodySegs[this->bodySegCount].scale
                                           : this->mouthRadiusRef;
    } else {
        bodyCyl->dim.height = this->heightRef * 0.75f;
        bodyCyl->dim.yShift = this->yShiftRef;
        mouthSph->dim.worldSphere.radius = this->mouthRadiusRef * (1.0f + this->bodySegs[this->bodySegCount].scaleMod) *
                                           this->bodySegs[this->bodySegCount].scale;
    }
    // Updates mouth sphere collider position dynamically; this provides a more consistent grab range from start to end.
    if (!TYPE_INVERT(this)) {
        mouthSph->dim.worldSphere.center.y =
            !this->reachUp ? mouthPos.y - this->actor.scale.y * 265.0f : mouthPos.y - this->actor.scale.y * 665.0f;
    } else {
        mouthSph->dim.worldSphere.center.y = !this->reachUp ? mouthPos.y : mouthPos.y + this->actor.scale.y * 535.0f;
    }
    // Mouth collider x/z position is recessed during part of reach function.
    segment = &this->bodySegs[this->bodySegCount - 1];
    f32 lerpMod = segment->height / segment->heightTarget;
    mouthSph->dim.worldSphere.center.x =
        this->actionFunc != EnRr_Reach ? mouthPos.x : F32_LERPIMP(this->actor.world.pos.x, mouthPos.x, lerpMod);
    mouthSph->dim.worldSphere.center.z =
        this->actionFunc != EnRr_Reach ? mouthPos.z : F32_LERPIMP(this->actor.world.pos.z, mouthPos.z, lerpMod);

    bodySph0->dim.worldSphere.center.x = this->bodySphPos[0].x;
    bodySph0->dim.worldSphere.center.y = this->bodySphPos[0].y;
    bodySph0->dim.worldSphere.center.z = this->bodySphPos[0].z;
    bodySph1->dim.worldSphere.center.x = this->bodySphPos[1].x;
    bodySph1->dim.worldSphere.center.y = this->bodySphPos[1].y;
    bodySph1->dim.worldSphere.center.z = this->bodySphPos[1].z;
    neckSph->dim.worldSphere.center.x = this->bodySphPos[2].x;
    neckSph->dim.worldSphere.center.y = this->bodySphPos[2].y;
    neckSph->dim.worldSphere.center.z = this->bodySphPos[2].z;

    bodySph0->dim.worldSphere.radius =
        this->bodyRadiusRef * (1.0f + this->bodySegs[1].scaleMod) * this->bodySegs[1].scale;
    bodySph1->dim.worldSphere.radius =
        this->bodyRadiusRef * (1.0f + this->bodySegs[2].scaleMod) * this->bodySegs[2].scale;
    neckSph->dim.worldSphere.radius =
        this->bodyRadiusRef * (1.0f + this->bodySegs[3].scaleMod) * this->bodySegs[3].scale;
}

void EnRr_Update(Actor* thisx, PlayState* play) {
    EnRr* this = THIS;
    s16 i;

    this->frameCount++;

    if (this->stunTimer == 0) {
        // Dyanmic texture scroll based on phase velocity with an added sine element for more organic scroll.
        f32 scrollIncrement = Math_SinF(this->segMovePhase);
        f32 yIncrement = (f32)this->segPhaseVel / 896.0f;
        this->scrollControl += (1.0f + yIncrement + (scrollIncrement * yIncrement) / 4.5f) / 4.0f; // Convert for Draw.
    }

    s16 oldPhase = (s16)(this->segMovePhase - this->segPhaseVel);
    s16 currentPhase = (s16)this->segMovePhase;
    bool phaseCrossed = (this->stunTimer == 0) && ((oldPhase ^ currentPhase) < 0);

    // Whenever the geometry crosses a physical peak/valley, decrement the cycle
    if (phaseCrossed) {
        if ((this->actionFunc == EnRr_GrabPlayer && this->playerInside) || (this->actionFunc != EnRr_GrabPlayer)) {
            DECR(this->phaseCycleCount); // One cycle is a half-circle.
        }
    }

    DECR(this->regrabTimer);
    DECR(this->invincibilityTimer);

    f32 focus = this->bodySphPos[2].y - this->actor.world.pos.y;
    Actor_SetFocus(&this->actor, focus);

    EnRr_UpdateBodySegments(this, play);

    if (!EnRr_CollisionCheck(this, play)) {
        EnRr_PlayerCollisionCheck(this, play);
    }

    this->actionFunc(this, play);

    f32 friction = this->retreat || this->actionFunc == EnRr_Reach || this->actionFunc == EnRr_GrabPlayer ||
                           this->actionFunc == EnRr_ScoopPlayer
                       ? 0.15f                                                  // Retreat friction.
                       : this->segPhaseVel / (this->segPhaseVelTarget * 10.0f); // Dynamic normal friction.
    Math_StepToF(&this->actor.speedXZ, 0.0f, friction);

    Actor_MoveXZGravity(&this->actor);

    EnRr_UpdateColliderHandler(this, play);

    if (this->actionFunc != EnRr_Death) {
        CollisionCheck_SetOC(play, &play->colChkCtx, &this->cylinder.base);
        CollisionCheck_SetAC(play, &play->colChkCtx, &this->bodySph.base);
        CollisionCheck_SetOC(play, &play->colChkCtx, &this->bodySph.base);
        if (this->actionFunc != EnRr_Reach) {
            this->bodySph.base.acFlags &= ~AC_HARD;
        } else if (this->actionFunc == EnRr_Reach) {
            CollisionCheck_SetAC(play, &play->colChkCtx, &this->cylinder.base);
        }
    } else {
        this->cylinder.base.ocFlags1 &= ~OC1_HIT;
        this->bodySph.base.ocFlags1 &= ~OC1_HIT;
        this->cylinder.base.acFlags &= ~AC_HIT;
        this->bodySph.base.acFlags &= ~AC_HIT;
    }

    Collider_UpdateCylinder(&this->actor, &this->cylinder);

    if (!TYPE_INVERT(this)) {
        Actor_UpdateBgCheckInfo(play, &this->actor, 15.0f, this->cylinder.dim.radius, this->heightRef,
                                0x1 | 0x4 | 0x8 | 0x10 | 0x40);
    } else {
        // Check if inverted Like Like is not touching a ceiling. If timer expires, switch params to normal.
        Actor_UpdateBgCheckInfo(play, &this->actor, this->heightRef, this->cylinder.dim.radius, this->heightRef,
                                0x1 | 0x2 | 0x4 | 0x8 | 0x10 | 0x40);
        Vec3f ceilingTarget;
        Vec3f hitPos;
        CollisionPoly* poly = NULL;
        s32 bgId;

        ceilingTarget.x = this->actor.world.pos.x;
        ceilingTarget.y = this->actor.world.pos.y + 300.0f;
        ceilingTarget.z = this->actor.world.pos.z;
        if (this->actor.bgCheckFlags & 0x10) {
            this->actor.velocity.y = 0.0f;
        } else if (!BgCheck_EntityLineTest1(&play->colCtx, &this->actor.world.pos, &ceilingTarget, &hitPos, &poly,
                                            false, false, true, true, &bgId)) {
            this->fallTimer++;
        }
        if (this->fallTimer > 20) {
            Math_SmoothStepToS(&this->actor.world.rot.z, 0x0, 8, 0x800, 0x200);
            this->actor.shape.rot.z = this->actor.world.rot.z;
            this->actor.shape.yOffset = this->heightRef;
            if (this->actor.velocity.y > 0.0f) {
                this->actor.velocity.y -= 0.25f;
            }
            this->actor.gravity = -0.4f;
            if (this->actor.world.rot.z == 0) {
                this->actor.shape.shadowDraw = NULL;
                this->actor.params = LIKE_LIKE_NORMAL;
            }
        }
    }

    EnRr_GenerateWaterEffects(this, play);

    if (this->ocPlayerTimer > 0) {
        Player* player = GET_PLAYER(play);

        if (!(player->swallowed)) {
            this->ocPlayerTimer--;
            if (this->ocPlayerTimer == 0) {
                this->cylinder.base.ocFlags1 |= OC1_TYPE_PLAYER;
                this->bodySph.base.ocFlags1 |= OC1_TYPE_PLAYER;
            }
        }
    }

    if (this->stunTimer == 0) {
        EnRr_UpdateStepToTargets(this, play);
    }
}

static inline void Matrix_MultVecZ(f32 z, Vec3f* src) {
    Vec3f v = { 0.0f, 0.0f, z };
    Matrix_MultVec3f(&v, src);
}

static inline void Matrix_MultVecX(f32 x, Vec3f* src) {
    Vec3f v = { x, 0.0f, 0.0f };
    Matrix_MultVec3f(&v, src);
}

void EnRr_DrawBottomCap(EnRr* this, PlayState* play, Mtx* segMtx, float baseRadius, u32 scrollFactor) {
    OPEN_DISPS(play->state.gfxCtx);

    Vtx* capVtx = Graph_Alloc(play->state.gfxCtx, 25 * sizeof(Vtx));
    int vtxPerRing = 24;

    // ==========================================
    // CENTER VERTEX (The Singularity)
    // ==========================================
    capVtx[0].n.ob[0] = 0;
    capVtx[0].n.ob[1] = 200;
    capVtx[0].n.ob[2] = 0;
    capVtx[0].n.flag = 0;

    // We map the center vertex to the Top-Middle of the Top-Right Quarter
    capVtx[0].n.tc[0] = 384; // Center of the [256 to 512] S-range
    capVtx[0].n.tc[1] = 0;   // Top of the T-range

    capVtx[0].n.n[0] = 0;
    capVtx[0].n.n[1] = -127;
    capVtx[0].n.n[2] = 0;
    capVtx[0].n.a = 255;

    // ==========================================
    // PERIMETER VERTICES (The Outer Edge)
    // ==========================================
    for (int v = 0; v < vtxPerRing; v++) {
        float angle = ((float)v / vtxPerRing) * (2.0f * M_PI);
        float x = cosf(angle) * baseRadius;
        float z = sinf(angle) * baseRadius;

        // Create a seamless ping-pong wave to prevent texture tearing at the seams
        float repProgress = (angle / (2.0f * M_PI)) * 6.0f;
        float waveUV = fabsf(fmodf(repProgress, 1.0f) - 0.5f) * 2.0f;

        // Map the wave strictly to the S-range of the Top-Right Quarter (256 to 512)
        float sCoord = 256.0f + (waveUV * 256.0f);

        capVtx[v + 1].n.ob[0] = (s16)x;
        capVtx[v + 1].n.ob[1] = 0;
        capVtx[v + 1].n.ob[2] = (s16)z;
        capVtx[v + 1].n.flag = 0;
        capVtx[v + 1].n.tc[0] = (s16)sCoord;

        // Map the perimeter to the Bottom of the Top-Right Quarter
        capVtx[v + 1].n.tc[1] = 256;

        capVtx[v + 1].n.n[0] = 0;
        capVtx[v + 1].n.n[1] = -127;
        capVtx[v + 1].n.n[2] = 0;
        capVtx[v + 1].n.a = 255;
    }

    // ==========================================
    // TEXTURE SETUP: FLESH + SLIME (2-Cycle)
    // ==========================================
    gSPClearGeometryMode(POLY_OPA_DISP++, G_TEXTURE_GEN | G_TEXTURE_GEN_LINEAR);
    gSPSetGeometryMode(POLY_OPA_DISP++, G_LIGHTING | G_SHADING_SMOOTH);
    gSPTexture(POLY_OPA_DISP++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON);

    gDPSetCycleType(POLY_OPA_DISP++, G_CYC_2CYCLE);
    gDPSetRenderMode(POLY_OPA_DISP++, G_RM_FOG_SHADE_A, G_RM_AA_ZB_OPA_SURF2);

    // ==========================================
    // ADD THE VANILLA COLORS:
    // ==========================================
    gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 255, 255, 255, 255);
    gDPSetEnvColor(POLY_OPA_DISP++, 0, 0, 0, 160); // 160 Alpha is the blending ratio!

    // ==========================================
    // THE VANILLA BLEND COMBINER:
    // ==========================================
    gDPSetCombineLERP(POLY_OPA_DISP++, 
        TEXEL0, TEXEL1, ENV_ALPHA, TEXEL1, 
        0, 0, 0, 1, 
        COMBINED, 0, SHADE, 0, 
        0, 0, 0, COMBINED);

    // Load Texture 1: Flesh (Tile 0)
    gDPLoadTextureBlock(POLY_OPA_DISP++, gLikeLikeBodyPattern1Tex, G_IM_FMT_RGBA, G_IM_SIZ_16b, 16, 16, 0,
                        G_TX_MIRROR | G_TX_WRAP, G_TX_NOMIRROR | G_TX_WRAP, 4, 4, G_TX_NOLOD, G_TX_NOLOD);

    // Load Texture 2: Slime (Tile 1)
    gDPLoadMultiBlock(POLY_OPA_DISP++, gLikeLikeBodyPattern2Tex, 0x0100, 1, G_IM_FMT_RGBA, G_IM_SIZ_16b, 16, 16, 0,
                      G_TX_MIRROR | G_TX_WRAP, G_TX_NOMIRROR | G_TX_WRAP, 4, 4, 0, 0);

    // Apply the Radial Scroll! (Translating the T-axis slides the slime from Center to Perimeter)
    gSPDisplayList(POLY_OPA_DISP++,
                   Gfx_TwoTexScroll(play->state.gfxCtx, 0, 0, 0, 16, 16, 1, 0, (-scrollFactor) & 0x7F, 16, 16));

    // ==========================================
    // DRAW LOOPS
    // ==========================================
    gSPMatrix(POLY_OPA_DISP++, &segMtx[0], G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gSPVertex(POLY_OPA_DISP++, capVtx, 25, 0);

    for (int v = 0; v < vtxPerRing; v += 2) {
        int p1 = v + 1;
        int p2 = v + 2;
        int p3 = (v + 2 == vtxPerRing) ? 1 : v + 3;
        gSP2Triangles(POLY_OPA_DISP++, 0, p1, p2, 0, 0, p2, p3, 0);
    }

    CLOSE_DISPS(play->state.gfxCtx);
}

void EnRr_DrawBody(EnRr* this, PlayState* play, Mtx* segMtx, float baseRadius, u32 scrollFactor) {
    OPEN_DISPS(play->state.gfxCtx);

    int vtxOffset[8];
    int currentOffset = 0;
    for (int seg = 0; seg < 8; seg++) {
        vtxOffset[seg] = currentOffset;
        currentOffset += (seg >= 5) ? 97 : 25; // 96+1 and 24+1 to close the seam
    }

    Vtx* vtx = Graph_Alloc(play->state.gfxCtx, 520 * sizeof(Vtx));

    for (int seg = 0; seg < 8; seg++) {
        int vtxPerRing = (seg >= 5) ? 96 : 24;
        int offset = vtxOffset[seg];

        float minPinchFactor = 1.0f;
        float maxLobeFactor = 1.0f;

        if (seg == 7) {
            minPinchFactor = 0.6f;
            maxLobeFactor = 0.8f;
        }

        float r_min = baseRadius * minPinchFactor;
        float r_max = baseRadius * maxLobeFactor;

        for (int v = 0; v <= vtxPerRing; v++) {
            int vtxIdx = offset + v;

            float angle = ((float)v / vtxPerRing) * (2.0f * M_PI);

            float rawWave = cosf(6.0f * angle);
            float sign = (rawWave > 0.0f) ? 1.0f : -1.0f;
            float wave = sign * powf(fabsf(rawWave), 1.5f);

            float radius = 0.5f * (r_min + r_max) + 0.5f * (r_max - r_min) * wave;
            float x = cosf(angle) * radius;
            float z = sinf(angle) * radius;

            float yOffset = 0.0f;
            if (seg == 7) {
                float heightMod = (wave + 1.0f) * 0.5f;
                yOffset = heightMod * 300.0f + 1000.0f;
            }

            // ==========================================
            // HORIZONTAL (S): Hardware Mirroring!
            // ==========================================
            // We want 6 total repeats. 1 repeat = 512.0f.
            // 6 * 512.0f = 3072.0f total units around the circumference!
            float sCoord = (angle / (2.0f * M_PI)) * 6144.0f + 512.0f;

            // ==========================================
            // VERTICAL (T): Flipped and spanned
            // ==========================================
            float tCoord;
            if (seg == 7) {
                tCoord = -2048.0f;
            } else {
                tCoord = -(seg * 256.0f);
            }

            vtx[vtxIdx].n.ob[0] = (s16)x;
            vtx[vtxIdx].n.ob[1] = (s16)yOffset;
            vtx[vtxIdx].n.ob[2] = (s16)z;
            vtx[vtxIdx].n.flag = 0;
            vtx[vtxIdx].n.tc[0] = (s16)sCoord;
            vtx[vtxIdx].n.tc[1] = (s16)tCoord;

            float len = sqrtf(x * x + z * z);
            if (len == 0.0f)
                len = 1.0f;

            // Calculate a basic Y-normal based on the segment slope
            float normalY = (seg == 7) ? 80.0f : 15.0f;

            // Re-normalize with the new Y included so we don't exceed 127
            float fullLen = sqrtf(x * x + normalY * normalY + z * z);

            vtx[vtxIdx].n.n[0] = (s8)((x / fullLen) * 127.0f);
            vtx[vtxIdx].n.n[1] = (s8)((normalY / fullLen) * 127.0f);
            vtx[vtxIdx].n.n[2] = (s8)((z / fullLen) * 127.0f);
            vtx[vtxIdx].n.a = 255;
        }
    }

    // ==========================================
    // 1. TEXTURE SETUP: FLESH + SLIME (2-Cycle Mirrored)
    // ==========================================
    gSPClearGeometryMode(POLY_OPA_DISP++, G_TEXTURE_GEN | G_TEXTURE_GEN_LINEAR);
    gSPSetGeometryMode(POLY_OPA_DISP++, G_LIGHTING | G_SHADING_SMOOTH);

    gSPTexture(POLY_OPA_DISP++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON);
    gDPSetCycleType(POLY_OPA_DISP++, G_CYC_2CYCLE);
    gDPSetRenderMode(POLY_OPA_DISP++, G_RM_FOG_SHADE_A, G_RM_AA_ZB_OPA_SURF2);

    // ==========================================
    // ADD THE VANILLA COLORS:
    // ==========================================
    gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 255, 255, 255, 255);
    gDPSetEnvColor(POLY_OPA_DISP++, 0, 0, 0, 160); // 160 Alpha is the blending ratio!

    // ==========================================
    // THE VANILLA BLEND COMBINER:
    // ==========================================
    gDPSetCombineLERP(POLY_OPA_DISP++, 
        TEXEL0, TEXEL1, ENV_ALPHA, TEXEL1, 
        0, 0, 0, 1, 
        COMBINED, 0, SHADE, 0, 
        0, 0, 0, COMBINED);

    // Load Texture 1: Flesh (Tile 0)
    gDPLoadTextureBlock(POLY_OPA_DISP++, gLikeLikeBodyPattern1Tex, G_IM_FMT_RGBA, G_IM_SIZ_16b, 16, 16, 0,
                        G_TX_MIRROR | G_TX_WRAP, G_TX_NOMIRROR | G_TX_WRAP, 4, 4, G_TX_NOLOD, G_TX_NOLOD);

    // Load Texture 2: Slime (Tile 1)
    gDPLoadMultiBlock(POLY_OPA_DISP++, gLikeLikeBodyPattern2Tex, 0x0100, 1, G_IM_FMT_RGBA, G_IM_SIZ_16b, 16, 16, 0,
                      G_TX_MIRROR | G_TX_WRAP, G_TX_NOMIRROR | G_TX_WRAP, 4, 4, 0, 0);
    gSPDisplayList(POLY_OPA_DISP++, Gfx_TwoTexScroll(play->state.gfxCtx, 0, 0, 0, 16, 16,    // Tile 0 Setup
                                                     1, 0, (-scrollFactor) & 0x7F, 16, 16)); // Tile 1 Setup

    // ==========================================
    // DRAW LOOPS
    // ==========================================

    // PHASE A (0 to 4)
    for (int seg = 0; seg < 4; seg++) {
        for (int chunk = 0; chunk < 2; chunk++) {
            int startVtx = chunk * 12;
            gSPMatrix(POLY_OPA_DISP++, &segMtx[seg], G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
            gSPVertex(POLY_OPA_DISP++, &vtx[vtxOffset[seg] + startVtx], 13, 0);
            gSPMatrix(POLY_OPA_DISP++, &segMtx[seg + 1], G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
            gSPVertex(POLY_OPA_DISP++, &vtx[vtxOffset[seg + 1] + startVtx], 13, 13);
            for (int v = 0; v < 12; v++) {
                gSP2Triangles(POLY_OPA_DISP++, v, v + 13, v + 1, 0, v + 1, v + 13, v + 14, 0);
            }
        }
    }

    // PHASE B (Transition 4 to 5)
    for (int chunk = 0; chunk < 4; chunk++) {
        int bStart = chunk * 6;
        int tStart = chunk * 24;
        gSPMatrix(POLY_OPA_DISP++, &segMtx[4], G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        gSPVertex(POLY_OPA_DISP++, &vtx[vtxOffset[4] + bStart], 7, 0);
        gSPMatrix(POLY_OPA_DISP++, &segMtx[5], G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        gSPVertex(POLY_OPA_DISP++, &vtx[vtxOffset[5] + tStart], 25, 7);
        for (int v = 0; v < 6; v++) {
            int b0 = v, b1 = v + 1;
            int t0 = 7 + (v * 4), t1 = t0 + 1, t2 = t0 + 2, t3 = t0 + 3, t4 = t0 + 4;
            gSP2Triangles(POLY_OPA_DISP++, b0, t0, t1, 0, b0, t1, t2, 0);
            gSP2Triangles(POLY_OPA_DISP++, b0, t2, b1, 0, b1, t2, t3, 0);
            gSP1Triangle(POLY_OPA_DISP++, b1, t3, t4, 0);
        }
    }

    // PHASE C (5 to 7)
    for (int seg = 5; seg < 7; seg++) {
        for (int chunk = 0; chunk < 8; chunk++) {
            int startVtx = chunk * 12;
            gSPMatrix(POLY_OPA_DISP++, &segMtx[seg], G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
            gSPVertex(POLY_OPA_DISP++, &vtx[vtxOffset[seg] + startVtx], 13, 0);
            gSPMatrix(POLY_OPA_DISP++, &segMtx[seg + 1], G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
            gSPVertex(POLY_OPA_DISP++, &vtx[vtxOffset[seg + 1] + startVtx], 13, 13);
            for (int v = 0; v < 12; v++) {
                gSP2Triangles(POLY_OPA_DISP++, v, v + 13, v + 1, 0, v + 1, v + 13, v + 14, 0);
            }
        }
    }

    gSPClearGeometryMode(POLY_OPA_DISP++, G_TEXTURE_GEN | G_TEXTURE_GEN_LINEAR);

    bool isSwallowing = (this->actionFunc == EnRr_ScoopPlayer || this->actionFunc == EnRr_GrabPlayer || this->actionFunc == EnRr_ThrowPlayer);

    if (isSwallowing) {
        gSPClearGeometryMode(POLY_OPA_DISP++, G_CULL_BACK);
    } else {
        gSPSetGeometryMode(POLY_OPA_DISP++, G_CULL_BACK);
    }

    CLOSE_DISPS(play->state.gfxCtx);
}

void EnRr_DrawMouthRecess(EnRr* this, PlayState* play, Mtx* segMtx, float baseRadius, u32 scrollFactor) {
    OPEN_DISPS(play->state.gfxCtx);

    // 3 Rings (Center, Star, Lips) * 97 = 291 Vertices
    Vtx* mouthCapVtx = Graph_Alloc(play->state.gfxCtx, 300 * sizeof(Vtx));

    for (int v = 0; v <= 96; v++) {
        float angle = ((float)v / 96.0f) * (2.0f * M_PI);
        float sCoord = ((angle / (2.0f * M_PI)) * 6144.0f) + 512.0f;

        float dirX = cosf(angle);
        float dirZ = sinf(angle);

        // --- SHARED MATH ---
        float rawWaveOut = cosf(6.0f * angle);
        float waveOut = ((rawWaveOut > 0.0f) ? 1.0f : -1.0f) * powf(fabsf(rawWaveOut), 1.5f);
        float heightMod_out = (waveOut + 1.0f) * 0.5f;

        // ==========================================
        // RING 2: OUTER LIPS
        // ==========================================
        float r_min_out = baseRadius * 0.6f;
        float r_max_out = baseRadius * 0.8f;
        float radius_out = 0.5f * (r_min_out + r_max_out) + 0.5f * (r_max_out - r_min_out) * waveOut;
        float yOffset_out = heightMod_out * 300.0f + 1000.0f;

        int outIdx = v + 194;
        mouthCapVtx[outIdx].n.ob[0] = (s16)(dirX * radius_out);
        mouthCapVtx[outIdx].n.ob[1] = (s16)yOffset_out;
        mouthCapVtx[outIdx].n.ob[2] = (s16)(dirZ * radius_out);
        mouthCapVtx[outIdx].n.flag = 0;
        mouthCapVtx[outIdx].n.tc[0] = (s16)sCoord;
        mouthCapVtx[outIdx].n.tc[1] = 0;
        mouthCapVtx[outIdx].n.n[0] = (s8)(dirX * 80.0f);
        mouthCapVtx[outIdx].n.n[1] = 80;
        mouthCapVtx[outIdx].n.n[2] = (s8)(dirZ * 80.0f);
        mouthCapVtx[outIdx].n.a = 255;

        // ==========================================
        // RING 1: MID STAR (The Inner Mouth Recess is a Star-Shaped Prism). 
        // ==========================================
        float R_max = baseRadius * 0.575f;
        float R_min = baseRadius * 0.325f * this->innerMouthScale;
        float P2_x = R_min * 0.975f;
        float P2_y = R_min * 0.5f;
        float local_angle = fmodf(angle, (float)M_PI / 3.0f);
        if (local_angle > (float)M_PI / 6.0f) {
            local_angle = ((float)M_PI / 3.0f) - local_angle;
        }

        float denominator = P2_y * cosf(local_angle) + (R_max - P2_x) * sinf(local_angle);
        float r_straight = (R_max * P2_y) / denominator;
        float innerScaleMod = CLAMP(1.0f - this->innerMouthScale * 0.5f, 0.25f, 1.0f);
        float yOffset_mid = heightMod_out * 150.0f + 800.0f * innerScaleMod;

        int midIdx = v + 97;
        mouthCapVtx[midIdx].n.ob[0] = (s16)(dirX * r_straight);
        mouthCapVtx[midIdx].n.ob[1] = (s16)yOffset_mid;
        mouthCapVtx[midIdx].n.ob[2] = (s16)(dirZ * r_straight);
        mouthCapVtx[midIdx].n.flag = 0;
        mouthCapVtx[midIdx].n.tc[0] = (s16)sCoord;
        mouthCapVtx[midIdx].n.tc[1] = 256;
        mouthCapVtx[midIdx].n.n[0] = (s8)(dirX * -30.0f);
        mouthCapVtx[midIdx].n.n[1] = 100;
        mouthCapVtx[midIdx].n.n[2] = (s8)(dirZ * -30.0f);
        mouthCapVtx[midIdx].n.a = 255;

        // ==========================================
        // RING 0: THROAT
        // ==========================================
        float throatRadius = this->grabState == 2 ? 0.0f : baseRadius * 0.15f * this->innerMouthScale;

        float x_in = dirX * throatRadius;
        float z_in = dirZ * throatRadius;

        int centerIdx = v;
        mouthCapVtx[centerIdx].n.ob[0] = (s16)x_in;
        mouthCapVtx[centerIdx].n.ob[1] = -200 * this->innerMouthScale;
        mouthCapVtx[centerIdx].n.ob[2] = (s16)z_in;
        mouthCapVtx[centerIdx].n.flag = 0;
        mouthCapVtx[centerIdx].n.tc[0] = (s16)sCoord;
        mouthCapVtx[centerIdx].n.tc[1] = 1024;
        mouthCapVtx[centerIdx].n.n[0] = 0;
        mouthCapVtx[centerIdx].n.n[1] = -127;
        mouthCapVtx[centerIdx].n.n[2] = 0;
        mouthCapVtx[centerIdx].n.a = 255;
    }

    // ==========================================
    // 2. TEXTURE SETUP: FOG AND LIGHTING
    // ==========================================
    gSPClearGeometryMode(POLY_OPA_DISP++, G_TEXTURE_GEN | G_TEXTURE_GEN_LINEAR);
    gSPSetGeometryMode(POLY_OPA_DISP++, G_LIGHTING | G_SHADING_SMOOTH);

    gSPSetGeometryMode(POLY_OPA_DISP++, G_LIGHTING | G_SHADING_SMOOTH);
    gSPTexture(POLY_OPA_DISP++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON);

    gDPSetCycleType(POLY_OPA_DISP++, G_CYC_2CYCLE);
    gDPSetRenderMode(POLY_OPA_DISP++, G_RM_FOG_SHADE_A, G_RM_AA_ZB_OPA_SURF2);

    // ==========================================
    // ADD THE VANILLA COLORS:
    // ==========================================
    gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 255, 255, 255, 255);
    gDPSetEnvColor(POLY_OPA_DISP++, 0, 0, 0, 160); // 160 Alpha is the blending ratio!

    // ==========================================
    // THE VANILLA BLEND COMBINER:
    // ==========================================
    // Cycle 1: Blends Flesh (TEXEL0) and Slime (TEXEL1) using the EnvColor Alpha
    // Cycle 2: Multiplies the beautifully blended result by the 3D Lighting (SHADE)
    gDPSetCombineLERP(POLY_OPA_DISP++, 
        TEXEL0, TEXEL1, ENV_ALPHA, TEXEL1, 
        0, 0, 0, 1, 
        COMBINED, 0, SHADE, 0, 
        0, 0, 0, COMBINED);

    // Load Texture 1: Flesh (Tile 0)
    gDPLoadTextureBlock(POLY_OPA_DISP++, gLikeLikeBodyPattern1Tex, G_IM_FMT_RGBA, G_IM_SIZ_16b, 16, 16, 0,
                        G_TX_MIRROR | G_TX_WRAP, G_TX_NOMIRROR | G_TX_WRAP, 4, 4, G_TX_NOLOD, G_TX_NOLOD);

    // Load Texture 2: Slime (Tile 1)
    gDPLoadMultiBlock(POLY_OPA_DISP++, gLikeLikeBodyPattern2Tex, 0x0100, 1, G_IM_FMT_RGBA, G_IM_SIZ_16b, 16, 16, 0,
                      G_TX_MIRROR | G_TX_WRAP, G_TX_NOMIRROR | G_TX_WRAP, 4, 4, 0, 0);

    gSPDisplayList(POLY_OPA_DISP++,
                   Gfx_TwoTexScroll(play->state.gfxCtx, 0, 0, 0, 16, 16, 1, 0, (scrollFactor) & 0x7F, 16, 16));

    // ==========================================
    // DRAW LOOPS (2 Identical Stages)
    // ==========================================

    // STAGE 1 (Outer Lips down to Mid Star)
    for (int chunk = 0; chunk < 8; chunk++) {
        int startVtx = chunk * 12;
        gSPMatrix(POLY_OPA_DISP++, &segMtx[7], G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        gSPVertex(POLY_OPA_DISP++, &mouthCapVtx[startVtx + 97], 13, 0);   // Star Ring (0-12)
        gSPVertex(POLY_OPA_DISP++, &mouthCapVtx[startVtx + 194], 13, 13); // Outer Ring (13-25)
        for (int v = 0; v < 12; v++) {
            gSP2Triangles(POLY_OPA_DISP++, v + 13, v, v + 14, 0, v, v + 1, v + 14, 0);
        }
    }

    // STAGE 2 (Mid Star down to Center Void)
    for (int chunk = 0; chunk < 8; chunk++) {
        int startVtx = chunk * 12;
        gSPMatrix(POLY_OPA_DISP++, &segMtx[7], G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        gSPVertex(POLY_OPA_DISP++, &mouthCapVtx[startVtx + 0], 13, 0);   // Center Ring (0-12)
        gSPVertex(POLY_OPA_DISP++, &mouthCapVtx[startVtx + 97], 13, 13); // Star Ring (13-25)
        for (int v = 0; v < 12; v++) {
            gSP2Triangles(POLY_OPA_DISP++, v + 13, v, v + 14, 0, v, v + 1, v + 14, 0);
        }
    }
    
    gSPClearGeometryMode(POLY_OPA_DISP++, G_TEXTURE_GEN | G_TEXTURE_GEN_LINEAR);

    bool isSwallowing = (this->actionFunc == EnRr_ScoopPlayer || this->actionFunc == EnRr_GrabPlayer || this->actionFunc == EnRr_ThrowPlayer);

    // Only use the if/else to turn the back-faces on and off!
    if (isSwallowing) {
        gSPClearGeometryMode(POLY_OPA_DISP++, G_CULL_BACK);
    } else {
        gSPSetGeometryMode(POLY_OPA_DISP++, G_CULL_BACK);
    }

    CLOSE_DISPS(play->state.gfxCtx);
}

void EnRr_DrawStomach(EnRr* this, PlayState* play, Mtx* segMtx, float baseRadius, u32 scrollFactor) {
    OPEN_DISPS(play->state.gfxCtx);

    Player* player = GET_PLAYER(play);

    // 1. Dynamic Radius (X/Z axis volume)
    f32 dynamicMaxRadius = (player->cylinder.dim.radius / this->actor.scale.x) * 1.2f;

    // 2. Dynamic Height (Y axis volume)
    f32 playerLocalHeight = (player->cylinder.dim.height * 0.625f) / this->actor.scale.y;

    // 3. The "Bag Pinch" Math
    // The stomach spans 7 full segments, which is roughly 3500 units tall natively.
    // If the player doesn't fill that whole height, we dynamically increase the sine exponent.
    // A higher exponent crushes the empty space at the top and bottom, tightly bagging the player!
    f32 heightFillRatio = playerLocalHeight / 3500.0f;
    f32 bagPinchExponent = 0.5f / CLAMP(heightFillRatio, 0.1f, 2.0f);

    // 8 Rings * 97 Vertices (96 + 1 for the seam) = 776 Vertices total. 800 is a safe allocation.
    Vtx* stomVtx = Graph_Alloc(play->state.gfxCtx, 800 * sizeof(Vtx));
    int vtxPerRing = 96;

for (int seg = 0; seg < 8; seg++) {
        int offset = seg * 97;
        float progress = (float)seg / 7.0f;

        float safeSine = fabsf(sinf(progress * (float)M_PI));
        float stomRadius = powf(safeSine, bagPinchExponent) * dynamicMaxRadius;
        float yOffset = 0.0f;

        if (seg == 0) {
            yOffset = 200.0f;
        } else if (seg == 7) {
            stomRadius = this->grabState == 2 ? 0.0f : baseRadius * 0.15f * this->innerMouthScale;
            yOffset = -200.0f * this->innerMouthScale;
        }

        // ==========================================
        // DUAL-SOURCE BIOLUMINESCENCE
        // ==========================================
        
        // 1. THE BOTTOM (Stomach Pulse)
        // Strongest at seg 0, completely dark at seg 7
        float pulse = (Math_SinS(this->segMovePhase) + 1.0f) * 0.5f;
        float bottomRatio = 1.0f - ((float)seg / 7.0f);
        float bottomIntensity = powf(bottomRatio, 2.0f);
        float stomachGlow = pulse * bottomIntensity;

        // 2. THE TOP (Outside Light Rushing In)
        // Strongest at seg 7, completely dark at seg 0
        float topRatio = (float)seg / 7.0f;
        float topIntensity = powf(topRatio, 2.0f); 
        
        // Convert innerMouthScale (0.2 to 1.5) into a pure brightness multiplier (0.0 to 1.3)
        float mouthFlare = this->innerMouthScale - 0.2f;
        if (mouthFlare < 0.0f) mouthFlare = 0.0f;
        float throatGlow = mouthFlare * topIntensity;

        // 3. COMBINE THE LIGHTS!
        float glow = stomachGlow + throatGlow;

        for (int v = 0; v <= vtxPerRing; v++) {
            float angle = ((float)v / vtxPerRing) * (2.0f * M_PI);
            int vtxIdx = offset + v;

            stomVtx[vtxIdx].v.ob[0] = (s16)(cosf(angle) * stomRadius);
            stomVtx[vtxIdx].v.ob[1] = (s16)yOffset;
            stomVtx[vtxIdx].v.ob[2] = (s16)(sinf(angle) * stomRadius);
            stomVtx[vtxIdx].v.flag = 0;

            stomVtx[vtxIdx].v.tc[0] = (s16)(((float)v / vtxPerRing) * 1024.0f);
            stomVtx[vtxIdx].v.tc[1] = (s16)(seg * 256.0f);

            // Add the combined glow to the dark, fleshy base color
            stomVtx[vtxIdx].v.cn[0] = 30 + (s8)(glow * 150);  // R
            stomVtx[vtxIdx].v.cn[1] = 5  + (s8)(glow * 50);   // G
            stomVtx[vtxIdx].v.cn[2] = 20 + (s8)(glow * 60);   // B
            stomVtx[vtxIdx].v.cn[3] = 255;
        }
    }
    // ==========================================
    // THE MAGIC TRICK: REVERSE CULLING
    // ==========================================
    // We clear G_CULL_BACK and G_LIGHTING
    gSPClearGeometryMode(POLY_OPA_DISP++, G_LIGHTING | G_CULL_BACK | G_TEXTURE_GEN | G_TEXTURE_GEN_LINEAR);

    // We explicitly set G_CULL_FRONT. The outside vanishes, the inside appears!
    gSPSetGeometryMode(POLY_OPA_DISP++, G_SHADING_SMOOTH | G_CULL_FRONT);
    gSPTexture(POLY_OPA_DISP++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON);

    // Render Setup: 1-Cycle, multiplying the Slime texture by our glowing Vertex Colors
    gDPSetCycleType(POLY_OPA_DISP++, G_CYC_1CYCLE);
    gDPSetRenderMode(POLY_OPA_DISP++, G_RM_AA_ZB_OPA_SURF, G_RM_AA_ZB_OPA_SURF2);
    gDPSetCombineMode(POLY_OPA_DISP++, G_CC_MODULATERGBA, G_CC_MODULATERGBA);

    // Load the Slime texture
    gDPLoadTextureBlock(POLY_OPA_DISP++, gLikeLikeBodyPattern2Tex, G_IM_FMT_RGBA, G_IM_SIZ_16b, 16, 16, 0,
                        G_TX_MIRROR | G_TX_WRAP, G_TX_NOMIRROR | G_TX_WRAP, 4, 4, G_TX_NOLOD, G_TX_NOLOD);

    gSPDisplayList(POLY_OPA_DISP++, Gfx_TexScroll(play->state.gfxCtx, 0, (-scrollFactor / 2) & 0x7F, 16, 16));

    // ==========================================
    // DRAW LOOPS
    // ==========================================
    for (int seg = 0; seg < 7; seg++) {
        // We now need 8 chunks of 12 to draw all 96 triangles!
        for (int chunk = 0; chunk < 8; chunk++) {
            int startVtx = chunk * 12;
            gSPMatrix(POLY_OPA_DISP++, &segMtx[seg], G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
            gSPVertex(POLY_OPA_DISP++, &stomVtx[(seg * 97) + startVtx], 13, 0);
            gSPMatrix(POLY_OPA_DISP++, &segMtx[seg + 1], G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
            gSPVertex(POLY_OPA_DISP++, &stomVtx[((seg + 1) * 97) + startVtx], 13, 13);
            
            for (int v = 0; v < 12; v++) {
                gSP2Triangles(POLY_OPA_DISP++, v, v + 13, v + 1, 0, v + 1, v + 13, v + 14, 0);
            }
        }
    }

    gSPClearGeometryMode(POLY_OPA_DISP++, G_CULL_FRONT);
    gSPSetGeometryMode(POLY_OPA_DISP++, G_CULL_BACK);

    CLOSE_DISPS(play->state.gfxCtx);
}

void EnRr_DrawAbyssPlane(EnRr* this, PlayState* play, Mtx* segMtx, float baseRadius) {
    OPEN_DISPS(play->state.gfxCtx);

    // ==========================================
    // VANILLA TEXTURE SETTINGS (From the DL!)
    // ==========================================
    int HOLE_TEX_SIZE = 16;   // MASKS 4 / MASKT 4 = 16x16
    int HOLE_TEX_MASK = 4;    
    float FULL_UV = 512.0f;   // 16 pixels * 32 = 512 max UV

    Vtx* abyssVtx = Graph_Alloc(play->state.gfxCtx, 300 * sizeof(Vtx));

    float max_extent = baseRadius * 0.8f;

    // ==========================================
    // EXACT MOUTH FUNNEL GEOMETRY (3 Rings)
    // ==========================================
    for (int v = 0; v <= 96; v++) {
        float linearProgress = (float)v / 96.0f;
        float angle = linearProgress * (2.0f * M_PI);

        float dirX = cosf(angle);
        float dirZ = sinf(angle);

        float rawWaveOut = cosf(6.0f * angle);
        float waveOut = ((rawWaveOut > 0.0f) ? 1.0f : -1.0f) * powf(fabsf(rawWaveOut), 1.5f);
        float heightMod_out = (waveOut + 1.0f) * 0.5f;

        // ==========================================
        // RING 2: OUTER LIPS
        // ==========================================
        float r_min_out = baseRadius * 0.6f;
        float r_max_out = baseRadius * 0.8f;
        float radius_out = 0.5f * (r_min_out + r_max_out) + 0.5f * (r_max_out - r_min_out) * waveOut;
        
        float yOffset_out = (heightMod_out * 300.0f + 1000.0f) + 3.0f; 
        float x_out = dirX * radius_out * 0.98f;
        float z_out = dirZ * radius_out * 0.98f;

        int outIdx = v + 194;
        abyssVtx[outIdx].v.ob[0] = (s16)x_out;
        abyssVtx[outIdx].v.ob[1] = (s16)yOffset_out;
        abyssVtx[outIdx].v.ob[2] = (s16)z_out;
        abyssVtx[outIdx].v.flag = 0;
        
        // MIRRORED PLANAR MAP: 0 is the center, 512 is the edge.
        // Negative physical coordinates are automatically flipped by G_TX_MIRROR!
        abyssVtx[outIdx].v.tc[0] = (s16)((x_out / max_extent) * FULL_UV);
        abyssVtx[outIdx].v.tc[1] = (s16)((z_out / max_extent) * FULL_UV);
        
        abyssVtx[outIdx].v.cn[0] = 255;
        abyssVtx[outIdx].v.cn[1] = 255;
        abyssVtx[outIdx].v.cn[2] = 255;
        abyssVtx[outIdx].v.cn[3] = 255;

        // ==========================================
        // RING 1: MID STAR
        // ==========================================
        float R_max = baseRadius * 0.575f; 
        float R_min = baseRadius * 0.325f * this->innerMouthScale;
        float P2_x = R_min * 0.975f;
        float P2_y = R_min * 0.5f;
        float local_angle = fmodf(angle, (float)M_PI / 3.0f);
        if (local_angle > (float)M_PI / 6.0f) {
            local_angle = ((float)M_PI / 3.0f) - local_angle;
        }

        float denominator = P2_y * cosf(local_angle) + (R_max - P2_x) * sinf(local_angle);
        float r_straight = (R_max * P2_y) / denominator; 
        float innerScaleMod = CLAMP(1.0f - this->innerMouthScale * 0.5f, 0.25f, 1.0f);
        
        float yOffset_mid = (heightMod_out * 150.0f + 800.0f * innerScaleMod) + 3.0f;
        float x_mid = dirX * r_straight * 0.98f;
        float z_mid = dirZ * r_straight * 0.98f;

        int midIdx = v + 97;
        abyssVtx[midIdx].v.ob[0] = (s16)x_mid;
        abyssVtx[midIdx].v.ob[1] = (s16)yOffset_mid;
        abyssVtx[midIdx].v.ob[2] = (s16)z_mid;
        abyssVtx[midIdx].v.flag = 0;
        
        abyssVtx[midIdx].v.tc[0] = (s16)((x_mid / max_extent) * FULL_UV);
        abyssVtx[midIdx].v.tc[1] = (s16)((z_mid / max_extent) * FULL_UV);  
        
        abyssVtx[midIdx].v.cn[0] = 255;
        abyssVtx[midIdx].v.cn[1] = 255;
        abyssVtx[midIdx].v.cn[2] = 255;
        abyssVtx[midIdx].v.cn[3] = 255;

        // ==========================================
        // RING 0: THROAT OPENING
        // ==========================================
        float throatRadius = baseRadius * 0.15f * this->innerMouthScale * 0.98f;
        
        float x_in = dirX * throatRadius;
        float z_in = dirZ * throatRadius;

        int centerIdx = v;
        abyssVtx[centerIdx].v.ob[0] = (s16)x_in;
        abyssVtx[centerIdx].v.ob[1] = -195; 
        abyssVtx[centerIdx].v.ob[2] = (s16)z_in;
        abyssVtx[centerIdx].v.flag = 0;
        
        abyssVtx[centerIdx].v.tc[0] = (s16)((x_in / max_extent) * FULL_UV);
        abyssVtx[centerIdx].v.tc[1] = (s16)((z_in / max_extent) * FULL_UV);
        
        abyssVtx[centerIdx].v.cn[0] = 255;
        abyssVtx[centerIdx].v.cn[1] = 255;
        abyssVtx[centerIdx].v.cn[2] = 255;
        abyssVtx[centerIdx].v.cn[3] = 255;
    }

    // ==========================================
    // TEXTURE SETUP & STANDARD COMBINER
    // ==========================================
    gSPClearGeometryMode(POLY_OPA_DISP++, G_LIGHTING | G_CULL_BACK | G_TEXTURE_GEN | G_TEXTURE_GEN_LINEAR);
    gSPSetGeometryMode(POLY_OPA_DISP++, G_SHADING_SMOOTH);
    gSPTexture(POLY_OPA_DISP++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON);

    gDPSetCycleType(POLY_OPA_DISP++, G_CYC_1CYCLE);
    gDPSetRenderMode(POLY_OPA_DISP++, G_RM_AA_ZB_OPA_SURF, G_RM_AA_ZB_OPA_SURF2);
    
    // As seen in your DL trace (Line 115)
    gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 255, 255, 255, 255);

    gDPSetCombineMode(POLY_OPA_DISP++, G_CC_MODULATEIA, G_CC_MODULATEIA);

    // Using the exact DL settings: FMT_IA, SIZ_16b, Size 16x16, with G_TX_MIRROR on both axes!
    gDPLoadTextureBlock(POLY_OPA_DISP++, gLikeLikeHoleTex, G_IM_FMT_IA, G_IM_SIZ_16b, HOLE_TEX_SIZE, HOLE_TEX_SIZE, 0,
                        G_TX_MIRROR, G_TX_MIRROR, HOLE_TEX_MASK, HOLE_TEX_MASK, G_TX_NOLOD, G_TX_NOLOD);

    // ==========================================
    // DRAW LOOPS
    // ==========================================
    for (int chunk = 0; chunk < 8; chunk++) {
        int startVtx = chunk * 12;
        gSPMatrix(POLY_OPA_DISP++, &segMtx[7], G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        gSPVertex(POLY_OPA_DISP++, &abyssVtx[startVtx + 97], 13, 0);   
        gSPVertex(POLY_OPA_DISP++, &abyssVtx[startVtx + 194], 13, 13); 
        for (int v = 0; v < 12; v++) {
            gSP2Triangles(POLY_OPA_DISP++, v + 13, v, v + 14, 0, v, v + 1, v + 14, 0);
        }
    }

    for (int chunk = 0; chunk < 8; chunk++) {
        int startVtx = chunk * 12;
        gSPMatrix(POLY_OPA_DISP++, &segMtx[7], G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        gSPVertex(POLY_OPA_DISP++, &abyssVtx[startVtx + 0], 13, 0);    
        gSPVertex(POLY_OPA_DISP++, &abyssVtx[startVtx + 97], 13, 13);  
        for (int v = 0; v < 12; v++) {
            gSP2Triangles(POLY_OPA_DISP++, v + 13, v, v + 14, 0, v, v + 1, v + 14, 0);
        }
    }

    CLOSE_DISPS(play->state.gfxCtx);
}

void EnRr_Draw(Actor* thisx, PlayState* play) {
    EnRr* this = (EnRr*)thisx;
    s32 i;
    Vec3f zeroVec = { 0.0f, 0.0f, 0.0f };
    EnRrStruct* segment;
    u32 scrollControl_fixed = (u32)(this->scrollControl * 4.0f);
    f32 scaleTarget;
    f32 scaleTargetY;

    int numRings = 8;
    float baseRadius = 2142.857143f;

    Mtx* segMtx = Graph_Alloc(play->state.gfxCtx, numRings * sizeof(Mtx));

    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    Gfx_SetupDL_25Xlu(play->state.gfxCtx);

    // MATRIX CALCULATIONS
    Matrix_Push();
    Matrix_Scale((1.0f + this->bodySegs[0].scaleMod) * this->bodySegs[0].scale, 1.0f,
                 (1.0f + this->bodySegs[0].scaleMod) * this->bodySegs[0].scale, MTXMODE_APPLY);
    MATRIX_TOMTX(&segMtx[0]);
    Matrix_Pop();

    for (i = 1; i < numRings; i++) {
        segment = &this->bodySegs[i];
        scaleTarget = segment->scale * (segment->scaleMod + 1.0f);
        scaleTargetY = this->actionFunc != EnRr_Reach && this->actionFunc != EnRr_ThrowPlayer
                           ? segment->scale * (segment->scaleMod + 1.0f)
                           : 1.0f;

        Matrix_Translate(0.0f, segment->height + 500.0f * (1.0f + segment->ySquishMod), 0.0f, MTXMODE_APPLY);
        Matrix_RotateZYX(segment->rot.x, segment->rot.y, segment->rot.z, MTXMODE_APPLY);
        Matrix_Push();
        Matrix_Scale(scaleTarget, scaleTargetY, scaleTarget, MTXMODE_APPLY);
        MATRIX_TOMTX(&segMtx[i]);
        Matrix_Pop();

        Matrix_MultVec3f(&zeroVec, &this->effectPos[i]);
        if (i == 2) {
            Matrix_MultVec3f(&zeroVec, &this->bodySphPos[0]);
        } else if (i == 4) {
            Matrix_MultVec3f(&zeroVec, &this->bodySphPos[1]);
        } else if (i == 6) {
            Matrix_MultVec3f(&zeroVec, &this->bodySphPos[2]);
            Matrix_MultVec3f(&zeroVec, &this->mouthPartPos);
        }
    }

    this->effectPos[0] = this->actor.world.pos;
    Matrix_MultVec3f(&zeroVec, &this->bodySphPos[3]);

    CLOSE_DISPS(play->state.gfxCtx);

    // ==========================================
    // DRAW CALLS
    // ==========================================
    EnRr_DrawBottomCap(this, play, segMtx, baseRadius, scrollControl_fixed);
    EnRr_DrawBody(this, play, segMtx, baseRadius, scrollControl_fixed);
    EnRr_DrawMouthRecess(this, play, segMtx, baseRadius, scrollControl_fixed);
    EnRr_DrawStomach(this, play, segMtx, baseRadius, scrollControl_fixed);
    //EnRr_DrawAbyssPlane(this, play, segMtx, baseRadius);

    // ==========================================
    // VANILLA PARTICLE EFFECTS (Unchanged)
    // ==========================================
    if (this->effectTimer != 0) {
        Vec3f effectPos;
        s16 effectTimer = this->effectTimer - 1;

        this->actor.colorFilterTimer++;
        if ((effectTimer & 1) == 0) {
            s32 segIndex = 8 - (effectTimer >> 2);

            if (this->actor.colorFilterParams & 0x4000) {
                EffectSsEnFire_SpawnVec3f(play, &this->actor, &effectPos, 100, 0, 0, -1);
            } else {
                EffectSsEnIce_SpawnFlyingVec3f(play, &this->actor, &effectPos, 150, 150, 150, 250, 235, 245, 255, 3.0f);
            }
        }
    }
}

/*
void EnRr_Draw(Actor* thisx, PlayState* play2) {
    PlayState* play = play2;
    EnRr* this = THIS;
    Mtx* mtx = Graph_Alloc(play->state.gfxCtx, this->bodySegCount * sizeof(Mtx));
    Vec3f* bodyPartPos;
    EnRrStruct* segment;
    u32 scrollControl_fixed = (u32)(this->scrollControl * 4.0f);
    f32 scaleTarget;
    f32 scaleTargetY;
    s16 i;
    Vec3f zeroVec = { 0.0f, 0.0f, 0.0f };

    OPEN_DISPS(play->state.gfxCtx);

    Gfx_SetupDL_25Opa(play->state.gfxCtx);

    gSPSegment(POLY_OPA_DISP++, 0x0C, mtx);
    gSPSegment(
        POLY_OPA_DISP++, 0x08,
        Gfx_TwoTexScroll(play->state.gfxCtx, 0, 0, 0, 0x20, 0x10, 1, 0, (-scrollControl_fixed) & 0x7F, 0x20, 0x10));

    Matrix_Push();
    Matrix_Scale((1.0f + this->bodySegs[0].scaleMod) * this->bodySegs[0].scale, 1.0f,
                 (1.0f + this->bodySegs[0].scaleMod) * this->bodySegs[0].scale, MTXMODE_APPLY);

    bodyPartPos = &this->bodyPartsPos[0];

    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);

    // LIKE_LIKE_BODYPART_0 - LIKE_LIKE_BODYPART_3
    Matrix_MultVecZ(1842.1053f, bodyPartPos++);
    Matrix_MultVecZ(-1842.1053f, bodyPartPos++);
    Matrix_MultVecX(1842.1053f, bodyPartPos++);
    Matrix_MultVecX(-1842.1053f, bodyPartPos++);
    Matrix_Pop();

    for (i = 1; i <= this->bodySegCount; i++) {
        segment = &this->bodySegs[i];
        scaleTarget = segment->scale * (segment->scaleMod + 1.0f);
        scaleTargetY = this->actionFunc != EnRr_Reach && this->actionFunc != EnRr_ThrowPlayer
                           ? segment->scale * (segment->scaleMod + 1.0f)
                           : 1.0f;

        Matrix_Translate(0.0f, segment->height + BASE_SEG_HEIGHT * (1.0f + segment->ySquishMod), 0.0f, MTXMODE_APPLY);
        Matrix_RotateZYX(segment->rot.x, segment->rot.y, segment->rot.z, MTXMODE_APPLY);
        Matrix_Push();
        Matrix_Scale(scaleTarget, scaleTargetY, scaleTarget, MTXMODE_APPLY);
        MATRIX_TOMTX(mtx);

        if ((i & 1) != 0) {
            Matrix_RotateY(0x2000, MTXMODE_APPLY);
        }

        // LIKE_LIKE_BODYPART_4 - LIKE_LIKE_BODYPART_7
        // LIKE_LIKE_BODYPART_8 - LIKE_LIKE_BODYPART_11
        // LIKE_LIKE_BODYPART_12 - LIKE_LIKE_BODYPART_15
        // LIKE_LIKE_BODYPART_16 - LIKE_LIKE_BODYPART_19
        Matrix_MultVecZ(1842.1053f, bodyPartPos++);
        Matrix_MultVecZ(-1842.1053f, bodyPartPos++);
        Matrix_MultVecX(1842.1053f, bodyPartPos++);
        Matrix_MultVecX(-1842.1053f, bodyPartPos++);
        Matrix_Pop();
        mtx++;
        if (i == 1) {
            Matrix_MultVec3f(&zeroVec, &this->bodySphPos[0]);
        } else if (i == 2) {
            Matrix_MultVec3f(&zeroVec, &this->bodySphPos[1]);
        } else if (i == 3) {
            Matrix_MultVec3f(&zeroVec, &this->bodySphPos[2]);
            Matrix_MultVec3f(&zeroVec, &this->mouthPartPos);
        }
    }
    Matrix_MultVec3f(&zeroVec, &this->bodySphPos[3]);

    gSPDisplayList(POLY_OPA_DISP++, gLikeLikeDL);

    CLOSE_DISPS(play->state.gfxCtx);

    if (this->effectTimer != 0) {
        s16 effectTimer = this->effectTimer - 1;
        this->actor.colorFilterTimer++;
        if (this->actor.colorFilterParams & 0x4000) {
            EffectSsEnFire_SpawnVec3f(play, &this->actor, this->bodyPartsPos,
                                      this->actor.scale.y * 66.66667f * this->drawDmgEffScale, 0, 0,
                                      &this->bodyPartsPos);
        } else {
            EffectSsEnIce_SpawnFlyingVec3f(play, &this->actor, this->bodyPartsPos, 150, 150, 150, this->drawDmgEffAlpha,
                                           235, 245, 255, this->drawDmgEffFrozenSteamScale);
        }
    }
} 
*/