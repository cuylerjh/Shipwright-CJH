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

#define FLAGS (ACTOR_FLAG_TARGETABLE | ACTOR_FLAG_HOSTILE | ACTOR_FLAG_UPDATE_WHILE_CULLED | ACTOR_FLAG_DRAW_WHILE_CULLED | ACTOR_FLAG_DRAGGED_BY_HOOKSHOT)

#define RR_MESSAGE_SHIELD (1 << 0)
#define RR_MESSAGE_TUNIC (1 << 1)
#define RR_MOUTH 4
#define RR_BASE 0

typedef enum {
    /* 0 */ REACH_NONE,
    /* 1 */ REACH_EXTEND,
    /* 2 */ REACH_STOP,
    /* 3 */ REACH_OPEN,
    /* 4 */ REACH_GAPE,
    /* 5 */ REACH_CLOSE
} EnRrReachState;

typedef enum {
	 /* 0 */ GRAB_TRY,
	 /* 1 */ GRAB_STEAL,
	 /* 2 */ GRAB_HOLD,
	 /* 3 */ GRAB_CLOSE
} EnRrGrabState;

typedef enum {
    /* 0x0 */ RR_DMG_NONE,
    /* 0x1 */ RR_DMG_STUN,
    /* 0x2 */ RR_DMG_FIRE,
    /* 0x3 */ RR_DMG_ICE,
    /* 0x4 */ RR_DMG_LIGHT_MAGIC,
    /* 0xB */ RR_DMG_LIGHT_ARROW = 11,
    /* 0xC */ RR_DMG_SHDW_ARROW,
    /* 0xD */ RR_DMG_WIND_ARROW,
    /* 0xE */ RR_DMG_SPRT_ARROW,
    /* 0xF */ RR_DMG_NORMAL
} EnRrDamageEffect;

typedef enum {
    /* 0 */ RR_DROP_RANDOM_RUPEE,
    /* 1 */ RR_DROP_MAGIC,
    /* 2 */ RR_DROP_ARROW,
    /* 3 */ RR_DROP_FLEXIBLE,
    /* 4 */ RR_DROP_RUPEE_PURPLE,
    /* 5 */ RR_DROP_RUPEE_RED
} EnRrDropType;

void EnRr_Init(Actor* thisx, PlayState* play);
void EnRr_Destroy(Actor* thisx, PlayState* play);
void EnRr_Update(Actor* thisx, PlayState* play);
void EnRr_Draw(Actor* thisx, PlayState* play);

void EnRr_InitBodySegments(EnRr* this, PlayState* play);

void EnRr_SetupDamage(EnRr* this);
void EnRr_SetupDeath(EnRr* this);

void EnRr_Approach(EnRr* this, PlayState* play);
void EnRr_Reach(EnRr* this, PlayState* play);
void EnRr_GrabPlayer(EnRr* this, PlayState* play);
void EnRr_ThrowPlayer(EnRr* this, PlayState* play);
void EnRr_Damage(EnRr* this, PlayState* play);
void EnRr_Death(EnRr* this, PlayState* play);
void EnRr_Retreat(EnRr* this, PlayState* play);
void EnRr_Stunned(EnRr* this, PlayState* play);

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

static char* sDropNames[] = {
    // "type 7", "small magic jar", "arrow", "fairy", "20 rupees", "50 rupees"
    "タイプ７  ", "魔法の壷小", "矢        ", "妖精      ", "20ルピー  ", "50ルピー  ",
};

//Base collider values
static ColliderCylinderInitType1 sCylinderInit1 = {
    {
        COLTYPE_HIT0, //MM behavior
        AT_NONE, 
        AC_ON | AC_TYPE_PLAYER,
        OC1_ON | OC1_TYPE_ALL, //MM behavior
        OC2_TYPE_1,
        COLSHAPE_CYLINDER,
    },
    {
        ELEMTYPE_UNK0,
        { 0xFFCFFFFF, 0x00, 0x08 },
        { 0xFFCFFFFF, 0x00, 0x00 },
        TOUCH_ON | TOUCH_SFX_NORMAL,
        BUMP_ON | BUMP_HOOKABLE,
        OCELEM_ON,
    },
    { 30, 55, 0, { 0, 0, 0 } },
};

//Base collider values
static ColliderCylinderInitType1 sCylinderInit2 = {
    {
        COLTYPE_HIT0, //---TEST; MM behavior, blue blood, white hitmark
        AT_NONE,
        AC_ON | AC_HARD | AC_TYPE_PLAYER,
        OC1_ON | OC1_NO_PUSH | OC1_TYPE_PLAYER,
        OC2_TYPE_1,
        COLSHAPE_CYLINDER,
    },
    {
        ELEMTYPE_UNK0,
        { 0xFFCFFFFF, 0x00, 0x08 },
        { 0xFFCFFFFF, 0x00, 0x00 },
        TOUCH_ON | TOUCH_SFX_NORMAL,
        BUMP_ON,
        OCELEM_ON,
    },
    { 25, 15, -10, { 0, 0, 0 } },
};

/* May change top collider to a sphere
static ColliderJntSphElementInit sJntSphElementsInit[] = {
    {
        {
            ELEMTYPE_UNK0,
            { 0xFFCFFFFF, 0x00, 0x08 },
            { 0xFFCFFFFF, 0x00, 0x00 },
            TOUCH_ON | TOUCH_SFX_NORMAL,
            BUMP_ON,
            OCELEM_ON,
        },
        { 11, { { 0, 0, 0 }, 20 }, 100 },
    },
};

static ColliderJntSphInit sJntSphInit = {
    {
        COLTYPE_HIT0,
        AT_NONE,
        AC_ON | AC_HARD | AC_TYPE_PLAYER,
        OC1_ON | OC1_NO_PUSH | OC1_TYPE_PLAYER,
        OC2_TYPE_1,
        COLSHAPE_JNTSPH,
    },
    ARRAY_COUNT(sJntSphElementsInit),
    sJntSphElementsInit,
};
*/

static DamageTable sDamageTable = {
    /* Deku nut      */ DMG_ENTRY(0, RR_DMG_NONE),
    /* Deku stick    */ DMG_ENTRY(2, RR_DMG_NORMAL),
    /* Slingshot     */ DMG_ENTRY(1, RR_DMG_NORMAL),
    /* Explosive     */ DMG_ENTRY(2, RR_DMG_NORMAL),
    /* Boomerang     */ DMG_ENTRY(0, RR_DMG_STUN),
    /* Normal arrow  */ DMG_ENTRY(2, RR_DMG_NORMAL),
    /* Hammer swing  */ DMG_ENTRY(2, RR_DMG_NORMAL),
    /* Hookshot      */ DMG_ENTRY(0, RR_DMG_NONE), //Changed to MM behavior
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
    /* Hammer jump   */ DMG_ENTRY(0, RR_DMG_NONE),
    /* Unknown 2     */ DMG_ENTRY(0, RR_DMG_NONE),
};

static InitChainEntry sInitChain[] = {
    ICHAIN_S8(naviEnemyId, 0x37, ICHAIN_CONTINUE),
    ICHAIN_U8(targetMode, 2, ICHAIN_CONTINUE),
    ICHAIN_F32(targetArrowOffset, 30, ICHAIN_STOP),
};

void EnRr_Init(Actor* thisx, PlayState* play2) {
    PlayState* play = play2;
    EnRr* this = (EnRr*)thisx;
    s32 i;

    Actor_ProcessInitChain(&this->actor, sInitChain);
    this->actor.colChkInfo.damageTable = &sDamageTable;

    if (this->actor.params == LIKE_LIKE_PARAM_0 || LIKE_LIKE_PARAM_3) { //Standard or inverted
        this->actor.colChkInfo.health = 7; 
        this->actor.scale.y = 0.014f;
        this->actor.scale.x = this->actor.scale.z = 0.01508f;
        this->collider1.dim.radius = this->collider1.dim.radius * 1.0769f;
        this->collider1.dim.height = this->collider1.dim.height * 1.0769f;
        this->collider2.dim.radius = this->collider2.dim.radius * 1.0769f;
        this->collider2.dim.height = this->collider2.dim.height * 1.0769f;
        this->collider2.dim.yShift = this->collider2.dim.yShift * 1.0769f;
        if (this->actor.params == LIKE_LIKE_PARAM_3) {
            this->actor.world.rot.z = this->actor.shape.rot.z = 32767;
            this->collider1.dim.yShift -= this->collider1.dim.height;
        }
    } else if (this->actor.params == LIKE_LIKE_PARAM_1) { //Child-size
        this->actor.colChkInfo.health = 5; 
        this->actor.scale.y = 0.0108f;
        this->actor.scale.x = this-> actor.scale.z = 0.01163f;
        this->collider1.dim.radius = this->collider1.dim.radius * 0.8308f;
        this->collider1.dim.height = this->collider1.dim.height * 0.8308f;
        this->collider2.dim.radius = this->collider2.dim.radius * 0.8308f;
        this->collider2.dim.height = this->collider2.dim.height * 0.8308f;
        this->collider2.dim.yShift = this->collider2.dim.yShift * 0.8308f;
        this->actor.colChkInfo.mass = 25;
    } else if (this->actor.params == LIKE_LIKE_PARAM_2) { //Giant
        this->actor.colChkInfo.health = 9; 
        this->actor.scale.x = this->actor.scale.z = 0.02154f;
        this->collider1.dim.radius = this->collider1.dim.radius * 1.5385f;
        this->collider1.dim.height = this->collider1.dim.height * 1.5385f;
        this->collider2.dim.radius = this->collider2.dim.radius * 1.5385f;
        this->collider2.dim.height = this->collider2.dim.height * 1.5385f;
        this->collider2.dim.yShift = this->collider2.dim.yShift * 1.5385f;
    }
    Collider_InitCylinder(play, &this->collider1);
    Collider_SetCylinderType1(play, &this->collider1, &this->actor, &sCylinderInit1);
    Collider_InitCylinder(play, &this->collider2);
    Collider_SetCylinderType1(play, &this->collider2, &this->actor, &sCylinderInit2);
    Actor_SetFocus(&this->actor, this->actor.scale.y * 2000.0f); //MM behavior
    //this->actor.colChkInfo.mass = MASS_IMMOVABLE; nullified - MM behavior
    this->actor.velocity.y = this->actor.speedXZ = 0.0f;
    if(this->actor.params == LIKE_LIKE_PARAM_3) {
        this->actor.gravity = 0.4f;
    } else {
        this->actor.gravity = -0.4f;
    }
    this->actionTimer = 0;
    this->eatenShield = 0;
    this->eatenTunic = 0;
    this->retreat = false;
    this->grabTimer = 0;
    this->grabDamagePlayer = 0;
    this->invincibilityTimer = 0;
    this->effectTimer = 0;
    this->hasPlayer = false;
    this->stopScroll = false;
    this->ocTimer = 0;
    this->colPlayerTimer = 0;
    this->frameDiv = 0;
    this->scrollDiv = 0;
    this->reachState = this->reachUp = this->grabState = this->isDead = false;
    this->actionFunc = EnRr_Approach;
    for (i = 0; i < 5; i++) {
        this->bodySegs[i].height = this->bodySegs[i].heightTarget = this->bodySegs[i].scaleMod.x =
            this->bodySegs[i].scaleMod.y = this->bodySegs[i].scaleMod.z = 0.0f;
    }
    EnRr_InitBodySegments(this, play);
}

void EnRr_Destroy(Actor* thisx, PlayState* play) {
    s32 pad;
    EnRr* this = (EnRr*)thisx;

    Collider_DestroyCylinder(play, &this->collider1);
    Collider_DestroyCylinder(play, &this->collider2);
}

void EnRr_SetSpeed(EnRr* this, f32 speed) {
    this->actor.speedXZ = speed;
    Audio_PlayActorSound2(&this->actor, NA_SE_EN_LIKE_WALK);
}

void EnRr_SetupReach(EnRr* this) { 
    s32 i;
    this->reachState = 2;
    this->actionTimer = 0; 
    this->segPhaseVelTarget = 2621.0f;
    this->segMoveRate = 0.0f;
    this->segMovePhase = 0; //---TEST, should make reach motion pulse more consistent

    if (!this->reachUp) {
        static f32 segmentHeightsA[] = { 0.0f, 500.0f, 750.0f, 1000.0f, 1250.0f };
        for (i = 1; i < 5; i++) { 
            this->bodySegs[i].heightTarget = segmentHeightsA[i];
            this->bodySegs[i].scaleTarget.x = 0.75f;
            this->bodySegs[i].rotTarget.x = 4096.0f + (this->actor.scale.x * 160692.971f); 
            this->bodySegs[i].rotTarget.z = 0.0f;
        }
    } else { //Additional functionality for reaching above
        static f32 segmentHeightsB[] = { 0.0f, 750.0f, 1000.0f, 1250.0f, 1500.0f };
        for (i = 1; i < 5; i++) { 
            this->bodySegs[i].heightTarget = segmentHeightsB[i];
            this->bodySegs[i].scaleTarget.x = 0.75f;
            this->bodySegs[i].rotTarget.x = 0.0f; 
            this->bodySegs[i].rotTarget.z = 0.0f;
        }
        this->reachUp = false;
    }
    this->actionFunc = EnRr_Reach;
    Audio_PlayActorSound2(&this->actor, NA_SE_EN_LIKE_UNARI); //unari = moan, groan, or loud noise
}

void EnRr_SetupNeutral(EnRr* this) {
    s32 i;

    this->reachState = 0;
    this->segMoveRate = 0.0f;
    this->segPhaseVelTarget = 2621.0f;
    for (i = 1; i < 5; i++) { 
        this->bodySegs[i].heightTarget = 0.0f;
        this->bodySegs[i].rotTarget.x = this->bodySegs[i].rotTarget.z = 0.0f;
        this->bodySegs[i].scaleTarget.x = 0.875f;
    }
    if (this->retreat) {
        this->actionTimer = 125; 
        this->actionFunc = EnRr_Retreat;
    } else {
        this->actionTimer = 50; 
        this->actionFunc = EnRr_Approach;
    }
}

void EnRr_SetupGrabPlayer(EnRr* this, Player* player) {
    s32 i;

    this->actionTimer = 0; //Cleans up any left over timer from reach action
    this->grabTimer = 64; 
    this->grabState = 1;
    this->grabDamagePlayer = 8;
    this->colPlayerTimer = 25; //MM behavior, during Approach this decrements to reset collider behavior
    this->collider1.base.ocFlags1 &= ~OC1_TYPE_PLAYER; //MM behavior, allows collisions with other objects w/o pushing player
    this->actor.flags &= ~ACTOR_FLAG_TARGETABLE;
    this->ocTimer = 8;
    this->hasPlayer = true;
    this->reachState = 0;
    this->frameCount = 0; //Makes sound trigger consistent
    this->segMoveRate = this->swallowOffset = this->actor.speedXZ = 0.0f;
    this->pulseSizeTarget = 0.15f;
    this->segPhaseVelTarget = 4096.0f;
    this->wobbleSizeTarget = 512.0f;
    for (i = 0; i < 5; i++) {
        this->bodySegs[i].heightTarget = 0.0f;
        this->bodySegs[i].rotTarget.x = this->bodySegs[i].rotTarget.z = 0.0f;
        this->bodySegs[i].scaleTarget.x = 1.0f;
    }
    this->actionFunc = EnRr_GrabPlayer;
    Audio_PlayActorSound2(&this->actor, NA_SE_EN_LIKE_DRINK);
}

u8 EnRr_GetMessage(u8 shield, u8 tunic) {
    u8 messageIndex = 0;

    if ((shield == 1 /* Deku shield */) || (shield == 2 /* Hylian shield */)) {
        messageIndex = RR_MESSAGE_SHIELD;
    }
    if ((tunic == 2 /* Goron tunic */) || (tunic == 3 /* Zora tunic */)) {
        messageIndex |= RR_MESSAGE_TUNIC;
    }

    return messageIndex;
}

void EnRr_SetupThrowPlayer(EnRr* this) {
    static f32 segmentHeightsC[] = { 0.0f, 250.0f, 500.0f, 750.0f, 1000.0f };
    s32 i;

    this->reachState = 1;
    this->actionTimer = 12; //---TEST
    this->segPhaseVelTarget = 2621.0f;
    this->segMoveRate = 0.0f;
    this->segMovePhase = 0; //---TEST 
    for (i = 1; i < 5; i++) { 
        this->bodySegs[i].heightTarget = segmentHeightsC[i];
        this->bodySegs[i].scaleTarget.x = 0.875f;
        if (this->actor.params != LIKE_LIKE_PARAM_3) {
            this->bodySegs[i].rotTarget.x = 4096.0f; 
        } else {
            this->bodySegs[i].rotTarget.x = 0.0f;
        }
        this->bodySegs[i].rotTarget.z = 0.0f;
        this->bodySegs[RR_MOUTH].scaleTarget.x = 0.75;
    }
    this->actionFunc = EnRr_ThrowPlayer;
}

void EnRr_SetupReleasePlayer(EnRr* this, PlayState* play) {
    Player* player = GET_PLAYER(play);

    this->actor.flags |= ACTOR_FLAG_TARGETABLE;
    this->hasPlayer = false;
    this->ocTimer = 50; 
    player->av2.actionVar2 = 150;
    this->segMoveRate = 0.0f;
    this->segPhaseVelTarget = 2621.0f;
    this->wobbleSizeTarget = 2816.0f;
    this->pulseSizeTarget = 0.15f;
    player->actor.parent = NULL;
    osSyncPrintf(VT_FGCOL(YELLOW) "%s[%d] : Rr_Catch_Cancel" VT_RST "\n", __FILE__, __LINE__);
    if (this->actor.params != LIKE_LIKE_PARAM_3) {
        func_8002F6D4(play, &this->actor, this->actor.scale.x * 230.786f, this->actor.shape.rot.y, this->actor.scale.y * 357.143f, 0); //Launch values modular; removed damage
    } else {
        func_8002F6D4(play, &this->actor, 0.0, this->actor.shape.rot.y, 0.0, 0); //Inverted
    }
    if (this->actor.colorFilterTimer == 0) {
        this->actionFunc = EnRr_Approach;
        Audio_PlayActorSound2(&this->actor, NA_SE_EN_LIKE_THROW);
    } else if (this->actor.colChkInfo.health != 0) {
        EnRr_SetupDamage(this);
    } else {
        EnRr_SetupDeath(this);
    }
}

void EnRr_SetupDamage(EnRr* this) {
    s32 i;

    this->reachState = 0;
    this->actionTimer = 0;
    this->segMoveRate = 0.0f;
    this->segPhaseVel = 1310.0f; 
    this->segPhaseVelTarget = 2621.0f;
    this->pulseSizeTarget = 0.0f;
    this->wobbleSizeTarget = 0.0f;
    for (i = 1; i < 5; i++) { 
        this->bodySegs[i].heightTarget = 0.0f;
        this->bodySegs[i].rotTarget.x = this->bodySegs[i].rotTarget.z = 0.0f;
        this->bodySegs[i].scaleTarget.x = 0.875f;
    }
    this->actionFunc = EnRr_Damage;
    Audio_PlayActorSound2(&this->actor, NA_SE_EN_LIKE_DAMAGE);
}

void EnRr_SetupApproach(EnRr* this) {
    s32 i;

    this->segMoveRate = 0.0f;
    this->pulseSizeTarget = 0.15f;
    this->segPhaseVelTarget = 2621.0f;
    this->wobbleSizeTarget = 2816.0f;
    for (i = 1; i < 5; i++) { 
        this->bodySegs[i].heightTarget = 0.0f;
        this->bodySegs[i].rotTarget.x = this->bodySegs[i].rotTarget.z = 0.0f;
        this->bodySegs[i].scaleTarget.x = 0.875f;
    }
    this->actionFunc = EnRr_Approach;
}

void EnRr_SetupDeath(EnRr* this) {
    s32 i;

    this->isDead = true;
    this->frameCount = 0;
    this->shrinkRate = 0.0f;
    this->segMoveRate = 0.0f;
    for (i = 0; i < 5; i++) {
        this->bodySegs[i].heightTarget = 0.0f;
        this->bodySegs[i].rotTarget.x = this->bodySegs[i].rotTarget.z = 0.0f;
    }
    this->actionFunc = EnRr_Death;
    Audio_PlayActorSound2(&this->actor, NA_SE_EN_LIKE_DEAD);
    this->actor.flags &= ~ACTOR_FLAG_TARGETABLE;
    GameInteractor_ExecuteOnEnemyDefeat(&this->actor);
}

void EnRr_SetupStunned(EnRr* this) {
    s32 i;

    this->stopScroll = true;
    this->segMovePhase = 0;
    this->segPhaseVel = 0.0f;
    this->segPhaseVelTarget = 2621.0f;
    this->segPulsePhaseDiff = 0.0f;
    this->segWobblePhaseDiffX = 0.0f;
    this->segWobbleXTarget = 4.0f;
    this->segWobblePhaseDiffZ = 0.0f;
    this->segWobbleZTarget = 2.0f;
    this->pulseSize = 0.0f;
    this->pulseSizeTarget = 0.15f;
    this->wobbleSize = 0.0f;
    this->wobbleSizeTarget = 2816.0f;
    for (i = 1; i < 5; i++) {
        this->bodySegs[i].scaleMod.y = 0.0f;
        this->bodySegs[i].rotTarget.x = 0.0f;
        this->bodySegs[i].rotTarget.y = 0.0f;
        this->bodySegs[i].rotTarget.z = 0.0f;
        this->bodySegs[i].scale.x = this->bodySegs[i].scale.y = this->bodySegs[i].scale.z =
        this->bodySegs[i].scaleTarget.x = this->bodySegs[i].scaleTarget.y = this->bodySegs[i].scaleTarget.z = 0.875f;
    }
    this->actionFunc = EnRr_Stunned;
}

//Marked for review
void EnRr_CollisionCheck(EnRr* this, PlayState* play) {
    Vec3f hitPos;
    Player* player = GET_PLAYER(play);

    if (this->collider2.base.acFlags & AC_HIT) {
        this->collider2.base.acFlags &= ~AC_HIT;
        // "Clank!!"
        osSyncPrintf(VT_FGCOL(GREEN) "カキン(%d)！！" VT_RST "\n", this->frameCount);
        hitPos.x = this->collider2.info.bumper.hitPos.x;
        hitPos.y = this->collider2.info.bumper.hitPos.y;
        hitPos.z = this->collider2.info.bumper.hitPos.z;
        CollisionCheck_SpawnShieldParticlesMetal2(play, &hitPos); //Change CollisionCheck type?
    } else {
        if (this->collider1.base.acFlags & AC_HIT) {
            u8 dropType = RR_DROP_RANDOM_RUPEE;

            this->collider1.base.acFlags &= ~AC_HIT;
            if (this->actor.colChkInfo.damageEffect != 0) {
                hitPos.x = this->collider1.info.bumper.hitPos.x;
                hitPos.y = this->collider1.info.bumper.hitPos.y;
                hitPos.z = this->collider1.info.bumper.hitPos.z;
                CollisionCheck_BlueBlood(play, NULL, &hitPos);
            }
            switch (this->actor.colChkInfo.damageEffect) {
                case RR_DMG_LIGHT_ARROW:
                    dropType++; // purple rupee
                case RR_DMG_SHDW_ARROW:
                    dropType++; // flexible
                case RR_DMG_WIND_ARROW:
                    dropType++; // arrow
                case RR_DMG_SPRT_ARROW:
                    dropType++; // magic jar
                case RR_DMG_NORMAL:
                    // "ouch"
                    osSyncPrintf(VT_FGCOL(RED) "いてっ( %d : LIFE %d : DAMAGE %d : %x )！！" VT_RST "\n",
                                 this->frameCount, this->actor.colChkInfo.health, this->actor.colChkInfo.damage,
                                 this->actor.colChkInfo.damageEffect);
                    this->stopScroll = false;
                    Actor_ApplyDamage(&this->actor);
                    this->invincibilityTimer = 40;
                    Actor_SetColorFilter(&this->actor, 0x4000, 0xFF, 0x2000, this->invincibilityTimer);
                    if (this->hasPlayer) {
                        EnRr_SetupReleasePlayer(this, play);
                    } else if (this->actor.colChkInfo.health != 0) {
                        EnRr_SetupDamage(this);
                    } else {
                        this->dropType = dropType;
                        EnRr_SetupDeath(this);
                    }
                    return;
                case RR_DMG_FIRE: // Fire Arrow and Din's Fire
                    Actor_ApplyDamage(&this->actor);
                    if (this->actor.colChkInfo.health == 0) {
                        this->dropType = RR_DROP_RANDOM_RUPEE;
                    }
                    Actor_SetColorFilter(&this->actor, 0x4000, 0xFF, 0x2000, 0x50);
                    this->effectTimer = 20;
                    EnRr_SetupStunned(this);
                    return;
                case RR_DMG_ICE: // Ice Arrow and unused ice magic
                    Actor_ApplyDamage(&this->actor);
                    if (this->actor.colChkInfo.health == 0) {
                        this->dropType = RR_DROP_RANDOM_RUPEE;
                    }
                    if (this->actor.colorFilterTimer == 0) {
                        this->effectTimer = 20;
                        Actor_SetColorFilter(&this->actor, 0, 0xFF, 0x2000, 0x50);
                    }
                    EnRr_SetupStunned(this);
                    return;
                case RR_DMG_LIGHT_MAGIC: // Unused light magic
                    Actor_ApplyDamage(&this->actor);
                    if (this->actor.colChkInfo.health == 0) {
                        this->dropType = RR_DROP_RUPEE_RED;
                    }
                    Actor_SetColorFilter(&this->actor, -0x8000, 0xFF, 0x2000, 0x50);
                    EnRr_SetupStunned(this);
                    return;
                case RR_DMG_STUN: // Boomerang; Hookshot removed
                    Audio_PlayActorSound2(&this->actor, NA_SE_EN_GOMA_JR_FREEZE);
                    Actor_SetColorFilter(&this->actor, 0, 0xFF, 0x2000, 0x50);
                    EnRr_SetupStunned(this);
                    return;
            }
        }
        if ((this->ocTimer == 0) && (this->actor.colorFilterTimer == 0) && (player->invincibilityTimer == 0) &&
            !(player->stateFlags2 & PLAYER_STATE2_GRABBED_BY_ENEMY) &&
            ((this->collider1.base.ocFlags1 & OC1_HIT) || (this->collider2.base.ocFlags1 & OC1_HIT))) {
            this->collider1.base.ocFlags1 &= ~OC1_HIT;
            this->collider2.base.ocFlags1 &= ~OC1_HIT;
            // "catch"
            osSyncPrintf(VT_FGCOL(GREEN) "キャッチ(%d)！！" VT_RST "\n", this->frameCount);
            if (play->grabPlayer(play, player)) {
                player->actor.parent = &this->actor;
                this->stopScroll = false;
                EnRr_SetupGrabPlayer(this, player);
            }
        }
    }
}

void EnRr_InitBodySegments(EnRr* this, PlayState* play) {
    s32 i;

    this->segMovePhase = 0;
    this->segPhaseVel = 0.0f;
    this->segPhaseVelTarget = 2621.0f;
    this->segPulsePhaseDiff = 0.0f;
    this->segWobblePhaseDiffX = 0.0f;
    this->segWobbleXTarget = 4.0f;
    this->segWobblePhaseDiffZ = 0.0f;
    this->segWobbleZTarget = 2.0f;
    this->pulseSize = 0.0f;
    this->pulseSizeTarget = 0.15f;
    this->wobbleSize = 0.0f;
    this->wobbleSizeTarget = 2816.0f;
    for (i = 0; i < 5; i++) {
        this->bodySegs[i].scaleMod.y = 0.0f;
        this->bodySegs[i].rotTarget.x = 0.0f;
        this->bodySegs[i].rotTarget.y = 0.0f;
        this->bodySegs[i].rotTarget.z = 0.0f;
        this->bodySegs[i].scale.x = this->bodySegs[i].scale.y = this->bodySegs[i].scale.z =
        this->bodySegs[i].scaleTarget.x = this->bodySegs[i].scaleTarget.y = this->bodySegs[i].scaleTarget.z = 0.875f;
    }
	 this->bodySegs[0].scale.x = this->bodySegs[0].scale.z = this->bodySegs[0].scaleTarget.x = this->bodySegs[0].scaleTarget.z  1.0f; //Bottom segment is always 1.0f
    for (i = 1; i < 5; i++) { //Bottom segment does not pulse, appears "rooted"
        this->bodySegs[i].scaleMod.x = this->bodySegs[i].scaleMod.z =
            Math_CosS(i * (u32)(s16)this->segPulsePhaseDiff * 0x1000) * this->pulseSize;
    }
    this->bodySegs[0].scaleMod.x = this->bodySegs[0].scaleMod.z = Math_CosS(0 * (u32)(s16)this->segPulsePhaseDiff * 0x1000) * (this->pulseSize / 2.0f);
    for (i = 1; i < 5; i++) {
        this->bodySegs[i].rotTarget.x = Math_CosS(i * (u32)(s16)this->segWobblePhaseDiffX * 0x1000) * this->wobbleSize;
        this->bodySegs[i].rotTarget.z = Math_SinS(i * (u32)(s16)this->segWobblePhaseDiffZ * 0x1000) * this->wobbleSize;
    }
}

void EnRr_UpdateBodySegments(EnRr* this, PlayState* play) {
    s32 i;
    s16 phase = this->segMovePhase;

    if (!this->isDead) {
        for (i = 1; i < 5; i++) { //Bottom segment does not pulse, appears "rooted"
            this->bodySegs[i].scaleMod.x = this->bodySegs[i].scaleMod.z =
                Math_CosS(phase + i * (s16)this->segPulsePhaseDiff * 0x1000) * this->pulseSize;
        }
        this->bodySegs[0].scaleMod.x = this->bodySegs[0].scaleMod.z = Math_CosS(0 * (u32)(s16)this->segPulsePhaseDiff * 0x1000) * (this->pulseSize / 2.0f);
        phase = this->segMovePhase;
        if (!this->isDead && (this->reachState == 0)) {
            for (i = 1; i < 5; i++) {
                this->bodySegs[i].rotTarget.x =
                    Math_CosS(phase + i * (s16)this->segWobblePhaseDiffX * 0x1000) * this->wobbleSize;
                this->bodySegs[i].rotTarget.z =
                    Math_SinS(phase + i * (s16)this->segWobblePhaseDiffZ * 0x1000) * this->wobbleSize;
            }
        }
    }
    if (!this->stopScroll) {
        this->segMovePhase += (s16)this->segPhaseVel;
    }
}

/* Added functionality pulled from WT water vortex; Like Like will try to draw player in
void EnStream_SuckPlayer(EnStream* this, PlayState* play) {
    Player* player = GET_PLAYER(play);
    s32 pad48;
    Vec3f posDifference;
    f32 xzDist;
    f32 yDistWithOffset;
    s32 pad30;
    s32 pad2C;

    if (func_80B0B81C(&this->actor.world.pos, &player->actor.world.pos, &posDifference, this->actor.scale.y) != 0) {
        xzDist = sqrtf(SQ(posDifference.x) + SQ(posDifference.z));
        yDistWithOffset = player->actor.world.pos.y - (this->actor.world.pos.y - 90.0f);
        player->pushedYaw = Math_FAtan2F(-posDifference.x, -posDifference.z) * (0x8000 / M_PI);
        if (xzDist > 3.0f) {
            Math_SmoothStepToF(&player->pushedSpeed, 3.0f, 0.5f, xzDist, 0.0f);
        } else {
            player->pushedSpeed = 0.0f;
            Math_SmoothStepToF(&player->actor.world.pos.x, this->actor.world.pos.x, 0.5f, 3.0f, 0.0f);
            Math_SmoothStepToF(&player->actor.world.pos.z, this->actor.world.pos.z, 0.5f, 3.0f, 0.0f);
        }
        if (yDistWithOffset > 0.0f) {
            Math_SmoothStepToF(&player->actor.velocity.y, -3.0f, 0.7f, yDistWithOffset, 0.0f);
        }
    } else {
        EnStream_SetupAction(this, EnStream_WaitForPlayer);
    }
}

void EnStream_WaitForPlayer(EnStream* this, PlayState* play) {
    Player* player = GET_PLAYER(play);
    s16 pad;
    Vec3f temp;

    if (func_80B0B81C(&this->actor.world.pos, &player->actor.world.pos, &temp, this->actor.scale.y) != 0) {
        EnStream_SetupAction(this, EnStream_SuckPlayer);
    }
}
*/

//Marked to modify, ---TEST SetupReach logic; 
void EnRr_Approach(EnRr* this, PlayState* play) {
    Math_SmoothStepToS(&this->actor.shape.rot.y, this->actor.yawTowardsPlayer, 0x10, 0x400, 0x40); //Yaw speed increased, ---TEST new minStep values
    this->actor.world.rot.y = this->actor.shape.rot.y;
    Player* player = GET_PLAYER(play);
    if ((this->actionTimer == 0) && !(player->stateFlags2 & PLAYER_STATE2_GRABBED_BY_ENEMY)) { //MM behavior
        if ((this->actor.yDistToPlayer < this->actor.scale.y * 6321.429f) && (this->actor.xyzDistToPlayerSq < SQ(this->actor.scale.x * 7500.0f))) { //reach trigger if within range
            EnRr_SetupReach(this); 
        } else if ((this->actor.yDistToPlayer > this->actor.scale.y * 6321.429f) && (this->actor.xyzDistToPlayerSq < SQ(this->actor.scale.y * 12642.857f))) {
            this->reachUp = true; //reachUp trigger if above actor
            EnRr_SetupReach(this); 
        }
    }
    if ((this->actor.xzDistToPlayer < (350.0f + (this->actor.scale.x * 7500.0f))) && (this->actor.speedXZ == 0.0f)) { 
        EnRr_SetSpeed(this, 0.0377f / this->actor.scale.x); 
    }
}

//Marked to modify
void EnRr_Reach(EnRr* this, PlayState* play) {
    Math_SmoothStepToS(&this->actor.shape.rot.y, this->actor.yawTowardsPlayer, 0x10, 0x400, 0x40); 
    this->actor.world.rot.y = this->actor.shape.rot.y;
    switch (this->reachState) {
        case REACH_EXTEND:
            if (this->actionTimer == 0) { 
                this->reachState = REACH_STOP;
            }
            break;
        case REACH_STOP:
            if (this->actionTimer == 0) { //Extended, wider scaleTarget.xz
                this->actionTimer = 9;
                this->bodySegs[RR_MOUTH].scaleTarget.x = 2.0f;
                this->reachState = REACH_OPEN;
            }
            break;
        case REACH_OPEN:
            if (this->actionTimer == 0) { //Slightly extended, slightly shorter heightTarget looks less distorted
                this->actionTimer = 3;
                this->bodySegs[RR_MOUTH].heightTarget = 1750.0f;
                this->reachState = REACH_GAPE;
            }
            break;
        case REACH_GAPE:
            if (this->actionTimer == 0) { //Extended to compensate, total action time matches vanilla
                //Trigger for REACH_SUCK state, needs development
                /*if (this->xyzDistToPlayerSq > (this->collider1.dim. * 4.5f) && this->xyzDistToPlayerSq < ) {
                    this->actionTimer = 15;
                    this-reachState = REACH_SUCK;
                } else {*/
                    this->actionTimer = 35;
                    this->bodySegs[RR_MOUTH].scaleTarget.x = 0.75f;
                    this->reachState = REACH_CLOSE;
                //}
            }
            break;
        //case REACH_SUCK:
            //if (this->actionTimer == 0)
        case REACH_CLOSE:
            if (this->actionTimer == 0) {
                EnRr_SetupNeutral(this);
            }
            break;
    }
}

void EnRr_GrabPlayer(EnRr* this, PlayState* play) {
    Player* player = GET_PLAYER(play);
    player->actor.velocity.y = 0.0f;
    this->grabTimer--;
    this->grabDamagePlayer--; //Deals incidental damage

    func_800AA000(this->actor.xyzDistToPlayerSq, 120, 2, 120);
    if (this->frameCount % this->frameDiv == 0) { //Sound play frequency modified by segPhaseVel
        Audio_PlayActorSound2(&this->actor, NA_SE_EN_LIKE_EAT);
    }
    this->ocTimer = 8;

    switch (this->grabState) {
        case GRAB_TRY:
            //Modular wobble/pulse helps give feedback to player
            this->wobbleSizeTarget = 512.0f + (player->av2.actionVar2 * 3.413f); //Caps at 1024
            this->pulseSizeTarget = 0.15f + (player->av2.actionVar2 / 6000.0f); //Caps at 0.175
            if (this->actor.params == LIKE_LIKE_PARAM_2 || this->actor.params == LIKE_LIKE_PARAM_3) {
                player->av2.actionVar2 -= 3;
            } else {
                player->av2.actionVar2 -= 2;
            }
            if (this->grabDamagePlayer == 0 && player->actor.colChkInfo.health > 16) { 
                play->damagePlayer(play, -1);
                this->grabDamagePlayer = 8;
            }
            if (this->grabTimer == 0) { 
                if (CUR_EQUIP_VALUE(EQUIP_TYPE_SHIELD) != EQUIP_VALUE_SHIELD_MIRROR || CUR_EQUIP_VALUE(EQUIP_TYPE_TUNIC) != EQUIP_VALUE_TUNIC_KOKIRI /* Rando Save File */) {
                    this->grabTimer = 90;
                    this->grabDamagePlayer = 5;
                    this->segPhaseVel = 5461.0f;
                    this->segPhaseVelTarget = 5461.0f; 
                    this->wobbleSizeTarget = 256.0f;
                    this->pulseSizeTarget = 0.135f;
                 	this->bodySegs[RR_MOUTH].scaleTarget.x = 0.875f;
                    Audio_PlayActorSound2(player, NA_SE_VO_LI_DAMAGE_S); //---TEST
                    //func_80064520(play, &play->csCtx); //---TEST
                    this->grabState = GRAB_STEAL;
                } else {
                    this->segPhaseVel = 4096.0f;
                    this->segPhaseVelTarget = 2560.0f;
                    this->wobbleSizeTarget = 768.0f;
                    this->pulseSizeTarget = 0.15f;
                    this->grabState = GRAB_HOLD;
                }
            }
            break;
        case GRAB_STEAL:
            player->av2.actionVar2 = 0; 
            if (this->grabDamagePlayer == 0 && player->actor.colChkInfo.health > 16) { 
                play->damagePlayer(play, -1);
                this->grabDamagePlayer = 5;
            }
            if (this->grabTimer == 0) {
                u8 tunic;
                u8 shield;

                tunic = 0;
                shield = 0;

                if (CUR_EQUIP_VALUE(EQUIP_TYPE_SHIELD) != EQUIP_VALUE_SHIELD_MIRROR) {
                    shield = Inventory_DeleteEquipment(play, EQUIP_TYPE_SHIELD);
                    if (shield != 0) {
                        this->eatenShield = shield;
                    }
                }
                if (CUR_EQUIP_VALUE(EQUIP_TYPE_TUNIC) != EQUIP_VALUE_TUNIC_KOKIRI && !IS_RANDO /* Rando Save File */) {
                    tunic = Inventory_DeleteEquipment(play, EQUIP_TYPE_TUNIC);
                    if (tunic != 0) {
                        this->eatenTunic = tunic;
                    }
                }
                switch (EnRr_GetMessage(shield, tunic)) {
                    case RR_MESSAGE_SHIELD:
                        Message_StartTextbox(play, 0x305F, NULL);
                        this->actor.colChkInfo.health += 2;
                        Audio_PlayActorSound2(&this->actor, NA_SE_EN_LIKE_DRINK); 
                        break;
                    case RR_MESSAGE_TUNIC:
                        Message_StartTextbox(play, 0x3060, NULL);
                        this->actor.colChkInfo.health += 2;
                        Audio_PlayActorSound2(&this->actor, NA_SE_EN_LIKE_DRINK); 
                        break;
                    case RR_MESSAGE_TUNIC | RR_MESSAGE_SHIELD:
                        Message_StartTextbox(play, 0x3061, NULL);
                        this->actor.colChkInfo.health += 3;
                        Audio_PlayActorSound2(&this->actor, NA_SE_EN_LIKE_DRINK); 
                        break;
                }

                this->grabTimer = 400;
                //this->grabDamagePlayer = 24;
                this->segPhaseVel = 3072.0f; 
                this->segPhaseVelTarget = 2560.0f;
                this->wobbleSizeTarget = 768.0f;
                this->pulseSizeTarget = 0.15f;
                //func_80064534(play, &play->csCtx);
                this->grabState = GRAB_HOLD;   
            }
            break;
        case GRAB_HOLD:
            player->av2.actionVar2 --;
            /*if (this->grabDamagePlayer == 0 && player.actor.chkColInfo.health > 16) { 
                play->damagePlayer(play, -1);
                this->grabDamagePlayer = this->frameDiv;
            }*/
            //Modular phase/wobble/pulse helps give feedback to player
            this->segPhaseVelTarget = 2560.0f - (player->av2.actionVar2 * 10.24); //Caps at 1536
            this->wobbleSizeTarget = 768.0f + (player->av2.actionVar2 * 10.24); //Caps at 1796
            this->pulseSizeTarget = 0.15f + (player->av2.actionVar2 / 4285.714f); //Caps at 0.185
            if (this->grabTimer == 0) {
                EnRr_SetupThrowPlayer(this);
            }
            break;
    if ((this->grabTimer == 0) || !(player->stateFlags2 & PLAYER_STATE2_GRABBED_BY_ENEMY)) {
        EnRr_SetupThrowPlayer(this); //MM behavior
    } else {
        Math_ApproachF(&player->actor.world.pos.x, this->mouthPos.x, 1.0f, 8.0f); 
        Math_ApproachF(&player->actor.world.pos.y, this->mouthPos.y + this->swallowOffset, 1.0f, 8.0f); 
        Math_ApproachF(&player->actor.world.pos.z, this->mouthPos.z, 1.0f, 8.0f); 
        Math_ApproachF(&this->swallowOffset, -this->collider1.dim.height, 1.0f, this->collider1.dim.height / (this->actor.scale.y * 1714.286f)); //MM behavior, rate modified by size
        }
    }
}

void EnRr_ThrowPlayer(EnRr* this, PlayState* play) {
    Player* player = GET_PLAYER(play);

    player->av2.actionVar2 = 0;
    player->actor.velocity.y = 0;
    player->actor.speedXZ = 0;
    Math_StepToF(&player->actor.world.pos.x, this->mouthPos.x, 8.0f);
    Math_StepToF(&player->actor.world.pos.y, this->mouthPos.y + this->swallowOffset, 8.0f);
    Math_StepToF(&player->actor.world.pos.z, this->mouthPos.z, 8.0f);
    Math_StepToF(&this->swallowOffset, -this->collider1.dim.height / 2.0f, this->collider1.dim.height / 20.0f);
    if (this->actionTimer == 0) {
        this->reachState = 0;
        EnRr_SetupGrabPlayer(this, player);
        EnRr_SetupNeutral(this);
    }
}

void EnRr_Damage(EnRr* this, PlayState* play) {
    s32 i;

    if (this->actor.colorFilterTimer == 0) {
        EnRr_SetupApproach(this);
    } else if ((this->actor.colorFilterTimer & 8) != 0) { 
        for (i = 1; i < 5; i++) {
            this->bodySegs[i].rotTarget.z = 4096.0f;
        }
    } else {
        for (i = 1; i < 5; i++) {
            this->bodySegs[i].rotTarget.z = -4096.0f;
        }
    }
}

void EnRr_Death(EnRr* this, PlayState* play) {
    s32 pad;
    s32 i;

    if (this->frameCount < 40) {
        for (i = 0; i < 5; i++) {
            Math_ApproachF(&this->bodySegs[i].heightTarget, i + 59 - (this->frameCount * 25.0f), 1.0f, 50.0f);
            this->bodySegs[i].scaleTarget.x = this->bodySegs[i].scaleTarget.z =
                (SQ(4 - i) * (f32)this->frameCount * 0.003f) + 1.0f;
        }
    } else if (this->frameCount >= 95) {
        Vec3f dropPos;

        dropPos.x = this->actor.world.pos.x;
        dropPos.y = this->actor.world.pos.y;
        dropPos.z = this->actor.world.pos.z;
        switch (this->eatenShield) {
            case 1:
                Item_DropCollectible(play, &dropPos, ITEM00_SHIELD_DEKU);
                break;
            case 2:
                Item_DropCollectible(play, &dropPos, ITEM00_SHIELD_HYLIAN);
                break;
        }
        switch (this->eatenTunic) {
            case 2:
                Item_DropCollectible(play, &dropPos, ITEM00_TUNIC_GORON);
                break;
            case 3:
                Item_DropCollectible(play, &dropPos, ITEM00_TUNIC_ZORA);
                break;
        }
        // "dropped"
        osSyncPrintf(VT_FGCOL(GREEN) "「%s」が出た！！" VT_RST "\n", sDropNames[this->dropType]);
        switch (this->dropType) {
            case RR_DROP_MAGIC:
                Item_DropCollectible(play, &dropPos, ITEM00_MAGIC_SMALL);
                break;
            case RR_DROP_ARROW:
                Item_DropCollectible(play, &dropPos, ITEM00_ARROWS_SINGLE);
                break;
            case RR_DROP_FLEXIBLE:
                Item_DropCollectible(play, &dropPos, ITEM00_FLEXIBLE);
                break;
            case RR_DROP_RUPEE_PURPLE:
                Item_DropCollectible(play, &dropPos, ITEM00_RUPEE_PURPLE);
                break;
            case RR_DROP_RUPEE_RED:
                Item_DropCollectible(play, &dropPos, ITEM00_RUPEE_RED);
                break;
            case RR_DROP_RANDOM_RUPEE:
            default:
                Item_DropCollectibleRandom(play, &this->actor, &dropPos, 12 << 4);
                break;
        }
        Actor_Kill(&this->actor);
    } else if (this->frameCount == 88) {
        Vec3f pos;
        Vec3f vel;
        Vec3f accel;

        pos.x = this->actor.world.pos.x;
        pos.y = this->actor.world.pos.y + 20.0f;
        pos.z = this->actor.world.pos.z;
        vel.x = 0.0f;
        vel.y = 0.0f;
        vel.z = 0.0f;
        accel.x = 0.0f;
        accel.y = 0.0f;
        accel.z = 0.0f;

        EffectSsDeadDb_Spawn(play, &pos, &vel, &accel, 100, 0, 255, 255, 255, 255, 255, 0, 0, 1, 9, true);
    } else {
        Math_ApproachF(&this->actor.scale.x, 0.0f, 1.0f, this->shrinkRate);
        Math_ApproachF(&this->shrinkRate, 0.001f, 1.0f, 0.00001f);
        this->actor.scale.z = this->actor.scale.x;
    }
}

void EnRr_Retreat(EnRr* this, PlayState* play) {
    if (this->actionTimer == 0) {
        this->retreat = false;
        this->segPhaseVelTarget = 2621.0f;
        this->actionFunc = EnRr_Approach;
    } else {
        Math_SmoothStepToS(&this->actor.shape.rot.y, this->actor.yawTowardsPlayer + 0x8000, 0x10, 0x600, 0x60); 
        this->actor.world.rot.y = this->actor.shape.rot.y;
        this->segPhaseVelTarget = 3643.0f;
        if (this->actor.speedXZ == 0.0f) {
            EnRr_SetSpeed(this, 0.0407 / this->actor.scale.x); //Faster during retreat, individual friction code
        }
    }
}

void EnRr_Stunned(EnRr* this, PlayState* play) {
    if (this->actor.colorFilterTimer == 0) {
        this->stopScroll = false;
        if (this->hasPlayer) {
            EnRr_SetupReleasePlayer(this, play);
        } else if (this->actor.colChkInfo.health != 0) {
            this->actionFunc = EnRr_Approach;
        } else {
            EnRr_SetupDeath(this);
        }
    }
}

/*Need to backport Ripple GFX from MM
void EnRr_GenerateRipple(EnRr* this, PlayState* play) {
    Vec3f sp2C;

    if ((this->actor.depthInWater < this->collider1.dim.height) && (this->actor.depthInWater > 1.0f) &&
        ((play->gameplayFrames % 9) == 0)) {
        sp2C.x = this->actor.world.pos.x;
        sp2C.y = this->actor.world.pos.y + this->actor.depthInWater;
        sp2C.z = this->actor.world.pos.z;
        EffectSsGRipple_Spawn(play, &sp2C, this->actor.scale.x * 34210.527f, this->actor.scale.x * 60526.316f, 0);
    }
}*/

//Marked to modify
void EnRr_Update(Actor* thisx, PlayState* play) {
    s32 pad;
    EnRr* this = (EnRr*)thisx;
    s32 i;

    this->frameCount++;
    if (this->segPhaseVel != 0.0f) {
        this->frameDiv = 32768 / (s16)this->segPhaseVel;
        this->scrollDiv = (s16)this->segPhase / 512;
    }
    if (!this->stopScroll) {
        this->scrollTimer++;
    }
    if (this->actionTimer != 0) {
        this->actionTimer--;
    }
    if (this->ocTimer != 0) {
        this->ocTimer--;
    }
    if (this->invincibilityTimer != 0) {
        this->invincibilityTimer--;
    }
    if (this->effectTimer != 0) {
        this->effectTimer--;
    }

    Actor_SetFocus(&this->actor, this->actor.scale.y * 2000.0f);
    EnRr_UpdateBodySegments(this, play);
    if (!this->isDead && ((this->actor.colorFilterTimer == 0) || !(this->actor.colorFilterParams & 0x4000))) {
        EnRr_CollisionCheck(this, play);
    }

    this->actionFunc(this, play);
    if (this->hasPlayer == 0x3F80) { // checks if 1.0f has been stored to hasPlayer's address
        assert(this->hasPlayer == 0x3F80);
    }
    if (!this->retreat) {
        Math_StepToF(&this->actor.speedXZ, 0.0f, 0.001508f / this->actor.scale.x); //Friction modified by size
    } else {
        Math_StepToF(&this->actor.speedXZ, 0.0f, 0.002262f / this->actor.scale.x); //Different friction during retreat
    }
    Actor_MoveForward(&this->actor);

    //EnRr_GenerateRipple(this, play);

    Collider_UpdateCylinder(&this->actor, &this->collider1); 
    this->collider2.dim.pos.x = this->mouthPos.x;
    this->collider2.dim.pos.y = this->mouthPos.y;
    this->collider2.dim.pos.z = this->mouthPos.z;
    //this->collider2.dim.rot.x = this->bodySegs[RR_MOUTH].rot.x; need another method for rotating colliders
    //this->collider2.dim.rot.z = this->bodySegs[RR_MOUTH].rot.z; 
    this->collider2.dim.radius *= this->bodySegs[RR_MOUTH].scale.x; 

    if (!this->isDead) { //Changed so that colliders are not affected by invincibility
        CollisionCheck_SetAC(play, &play->colChkCtx, &this->collider1.base);
        CollisionCheck_SetAC(play, &play->colChkCtx, &this->collider2.base);
        //if (this->ocTimer == 0) { //Changed so collider1 is always active; ocTimer now only affects grab
        CollisionCheck_SetOC(play, &play->colChkCtx, &this->collider1.base);
        CollisionCheck_SetOC(play, &play->colChkCtx, &this->collider2.base);
        if (this->colPlayerTimer > 0) {
            Player* player = GET_PLAYER(play);
            if (!(player->stateFlags2 & PLAYER_STATE2_GRABBED_BY_ENEMY)) {
                this->colPlayerTimer--;
            }
            if (this->colPlayerTimer == 0) {
                this->collider1.base.ocFlags1 |= OC1_TYPE_PLAYER;
            }
        }
    } else {
        this->collider2.base.ocFlags1 &= ~OC1_HIT;
        this->collider2.base.acFlags &= ~AC_HIT;
        this->collider1.base.ocFlags1 &= ~OC1_HIT;
        this->collider1.base.acFlags &= ~AC_HIT;
    }
    Actor_UpdateBgCheckInfo(play, &this->actor, 20.0f, 30.0f, 20.0f, 7);
    if (!this->stopScroll) {
        Math_ApproachF(&this->segPhaseVel, this->segPhaseVelTarget, 1.0f, 32.0f); //Reduced rate
        Math_ApproachF(&this->segPulsePhaseDiff, 4.0f, 1.0f, 5.0f);
        Math_ApproachF(&this->segWobblePhaseDiffX, this->segWobbleXTarget, 1.0f, 4.0f); //Changed to instant
        Math_ApproachF(&this->segWobblePhaseDiffZ, this->segWobbleZTarget, 1.0f, 2.0f); //Changed to instant
        Math_ApproachF(&this->pulseSize, this->pulseSizeTarget, 1.0f, 0.00125f); //Reduced rate
        Math_ApproachF(&this->wobbleSize, this->wobbleSizeTarget, 1.0f, 32.0f); //Reduced rate
        for (i = 1; i < 5; i++) { 
            Math_SmoothStepToS(&this->bodySegs[i].rot.x, this->bodySegs[i].rotTarget.x, 5, this->segMoveRate * 512.0f,
                               0); //Reduced rate
            if (this->actionFunc != EnRr_Damage) { //Separate segMoveRate multiplier during Damage
                Math_SmoothStepToS(&this->bodySegs[i].rot.z, this->bodySegs[i].rotTarget.z, 5, this->segMoveRate * 512.0f, 0); //Reduced rate
            } else {
                Math_SmoothStepToS(&this->bodySegs[i].rot.z, this->bodySegs[i].rotTarget.z, 5, this->segMoveRate * 1024.0f, 0); //Changed rate during damage
                 }
            Math_ApproachF(&this->bodySegs[i].scale.x, this->bodySegs[i].scaleTarget.x, 1.0f, this->segMoveRate * 0.175f); //Reduced rate
            this->bodySegs[i].scale.z = this->bodySegs[i].scale.x;
            this->actor.scale.z = this->actor.scale.x;
            Math_ApproachF(&this->bodySegs[i].height, this->bodySegs[i].heightTarget, 1.0f, this->segMoveRate * 150.0f); //Reduced rate
        }
        Math_ApproachF(&this->segMoveRate, 1.0f, 1.0f, 0.2f);
    }
}

static Vec3f sEffectOffsets[] = {
    { 25.0f, 0.0f, 0.0f },
    { -25.0f, 0.0f, 0.0f },
    { 0.0f, 0.0f, 25.0f },
    { 0.0f, 0.0f, -25.0f },
};

void EnRr_Draw(Actor* thisx, PlayState* play) {
    s32 pad;
    Vec3f zeroVec;
    EnRr* this = (EnRr*)thisx;
    s32 i;
    Mtx* segMtx = Graph_Alloc(play->state.gfxCtx, 4 * sizeof(Mtx));

    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Xlu(play->state.gfxCtx);
    gSPSegment(POLY_XLU_DISP++, 0x0C, segMtx);
    gSPSegment(POLY_XLU_DISP++, 0x08,
               Gfx_TwoTexScroll(play->state.gfxCtx, 0, (this->scrollTimer * 0) & 0x7F,
                                (this->scrollTimer * 0) & 0x3F, 32, 16, 1, (this->scrollTimer * 0) & 0x3F,
                                (this->scrollTimer * -scrollDiv) & 0x7F, 32, 16)); //TexScroll actively affected by segPhaseVel
    Matrix_Push();

    Matrix_Scale((1.0f + this->bodySegs[RR_BASE].scaleMod.x) * this->bodySegs[RR_BASE].scale.x,
                 (1.0f + this->bodySegs[RR_BASE].scaleMod.y) * this->bodySegs[RR_BASE].scale.y,
                 (1.0f + this->bodySegs[RR_BASE].scaleMod.z) * this->bodySegs[RR_BASE].scale.z, MTXMODE_APPLY);
    gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx),
              G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    Matrix_Pop();
    zeroVec.x = 0.0f;
    zeroVec.y = 0.0f;
    zeroVec.z = 0.0f;
    for (i = 1; i < 5; i++) {
        Matrix_Translate(0.0f, this->bodySegs[i].height + 1000.0f, 0.0f, MTXMODE_APPLY);

        Matrix_RotateZYX(this->bodySegs[i].rot.x, this->bodySegs[i].rot.y, this->bodySegs[i].rot.z, MTXMODE_APPLY);
        Matrix_Push();
        Matrix_Scale((1.0f + this->bodySegs[i].scaleMod.x) * this->bodySegs[i].scale.x,
                     (1.0f + this->bodySegs[i].scaleMod.y) * this->bodySegs[i].scale.y,
                     (1.0f + this->bodySegs[i].scaleMod.z) * this->bodySegs[i].scale.z, MTXMODE_APPLY);
        MATRIX_TOMTX(segMtx);
        Matrix_Pop();
        segMtx++;
        Matrix_MultVec3f(&zeroVec, &this->effectPos[i]);
    }
    this->effectPos[0] = this->actor.world.pos;
    Matrix_MultVec3f(&zeroVec, &this->mouthPos);
    gSPDisplayList(POLY_XLU_DISP++, gLikeLikeDL);

    CLOSE_DISPS(play->state.gfxCtx);
    if (this->effectTimer != 0) {
        Vec3f effectPos;
        s16 effectTimer = this->effectTimer - 1;

        this->actor.colorFilterTimer++;
        if ((effectTimer & 1) == 0) {
            s32 segIndex = 4 - (effectTimer >> 2);
            s32 offIndex = (effectTimer >> 1) & 3;

            effectPos.x = this->effectPos[segIndex].x + sEffectOffsets[offIndex].x + Rand_CenteredFloat(10.0f);
            effectPos.y = this->effectPos[segIndex].y + sEffectOffsets[offIndex].y + Rand_CenteredFloat(10.0f);
            effectPos.z = this->effectPos[segIndex].z + sEffectOffsets[offIndex].z + Rand_CenteredFloat(10.0f);
            if (this->actor.colorFilterParams & 0x4000) {
                EffectSsEnFire_SpawnVec3f(play, &this->actor, &effectPos, 100, 0, 0, -1);
            } else {
                EffectSsEnIce_SpawnFlyingVec3f(play, &this->actor, &effectPos, 150, 150, 150, 250, 235, 245, 255,
                                               3.0f);
            }
        }
    }
}