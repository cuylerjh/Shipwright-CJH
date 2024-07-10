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

#define FLAGS                                                                                                     \
    (ACTOR_FLAG_TARGETABLE | ACTOR_FLAG_HOSTILE | ACTOR_FLAG_UPDATE_WHILE_CULLED | ACTOR_FLAG_DRAW_WHILE_CULLED | \
     ACTOR_FLAG_DRAGGED_BY_HOOKSHOT)

#define RR_MESSAGE_SHIELD (1 << 0)
#define RR_MESSAGE_TUNIC (1 << 1)
#define RR_MOUTH 4
#define RR_BASE 0

typedef enum {
    /* 0 */ REACH_NONE,
    /* 1 */ REACH_EXTEND,
    /* 2 */ REACH_OPEN,
    /* 3 */ REACH_GAPE,
    /* 4 */ REACH_CLOSE
} EnRrReachState;

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

static ColliderCylinderInitType1 sCylinderInit1 = {
    {
        COLTYPE_HIT0,
        AT_NONE,
        AC_ON | AC_TYPE_PLAYER,
        OC1_ON | OC1_TYPE_ALL,
        COLSHAPE_CYLINDER,
    },
    {
        ELEMTYPE_UNK0,
        { 0xFFCFFFFF, 0x00, 0x08 },
        { 0xFFCFFFFF, 0x00, 0x00 },
        TOUCH_ON | TOUCH_SFX_NONE,
        BUMP_ON | BUMP_HOOKABLE,
        OCELEM_ON,
    },
    { 30, 53, 0, { 0, 0, 0 } },
};

static ColliderCylinderInitType1 sCylinderInit2 = {
    {
        COLTYPE_HIT3,
        AT_NONE,
        AC_ON | AC_HARD | AC_TYPE_PLAYER,
        OC1_ON | OC1_NO_PUSH | OC1_TYPE_PLAYER,
        COLSHAPE_CYLINDER,
    },
    {
        ELEMTYPE_UNK0,
        { 0xFFCFFFFF, 0x00, 0x08 },
        { 0xFFCFFFFF, 0x00, 0x00 },
        TOUCH_ON | TOUCH_SFX_NONE,
        BUMP_ON,
        OCELEM_ON,
    },
    { 20, 20, -10, { 0, 0, 0 } },
};

static DamageTable sDamageTable = {
    /* Deku nut      */ DMG_ENTRY(0, RR_DMG_NONE),
    /* Deku stick    */ DMG_ENTRY(2, RR_DMG_NORMAL),
    /* Slingshot     */ DMG_ENTRY(1, RR_DMG_NORMAL),
    /* Explosive     */ DMG_ENTRY(2, RR_DMG_NORMAL),
    /* Boomerang     */ DMG_ENTRY(0, RR_DMG_STUN),
    /* Normal arrow  */ DMG_ENTRY(2, RR_DMG_NORMAL),
    /* Hammer swing  */ DMG_ENTRY(2, RR_DMG_NORMAL),
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
    Collider_InitCylinder(play, &this->collider1);
    Collider_SetCylinderType1(play, &this->collider1, &this->actor, &sCylinderInit1);
    Collider_InitCylinder(play, &this->collider2);
    Collider_SetCylinderType1(play, &this->collider2, &this->actor, &sCylinderInit2);

    this->actor.colChkInfo.mass = 100;
    this->actor.colChkInfo.health = 7;
    if (this->actor.params > 4 || this->actor.params < 0) {
        this->actor.params = LIKE_LIKE_NORMAL;
    }
    
    if (this->actor.params == LIKE_LIKE_NORMAL || this->actor.params == LIKE_LIKE_INVERT ||
        this->actor.params == RUPEE_LIKE) {
        this->actor.scale.y = 0.015f;
        if (this->actor.params == LIKE_LIKE_INVERT) {
            this->actor.shape.rot.z = this->actor.world.rot.z = 32768.0f;
            this->collider1.dim.yShift -= this->collider1.dim.height;
        }
    } else if (this->actor.params == LIKE_LIKE_SMALL) {
        this->actor.scale.y = 0.01125f;
        this->actor.colChkInfo.health = 5;
        this->actor.colChkInfo.mass *= this->actor.scale.y / 0.013f;
    } else if (this->actor.params == LIKE_LIKE_GIANT) {
        this->actor.scale.y = 0.0225f;
        this->actor.colChkInfo.health = 9;
        this->actor.colChkInfo.mass *= this->actor.scale.y / 0.013f;
    }
    
    this->actor.scale.x = this->actor.scale.z = this->actor.scale.y * (0.014f / 0.013f);
    this->collider1.dim.radius *= this->actor.scale.y / 0.013f;
    this->collider1.dim.height *= this->actor.scale.y / 0.013f;
    this->collider1.dim.yShift *= this->actor.scale.y / 0.013f;
    this->collider2.dim.radius *= this->actor.scale.y / 0.013f;
    this->collider2.dim.height *= this->actor.scale.y / 0.013f;
    this->collider2.dim.yShift *= this->actor.scale.y / 0.013f;

    if (this->actor.params != LIKE_LIKE_INVERT) {
        this->actor.gravity = -0.4f;
    } else {
        this->actor.gravity = 0.0f;
    }

    Actor_SetFocus(&this->actor, this->actor.scale.y * 2000.0f);
    this->actor.velocity.y = this->actor.speedXZ = 0.0f;
    this->actionTimer = 0;
    this->eatenRupees = 0;
    this->eatenShield = 0;
    this->eatenTunic = 0;
    this->msgShield = 0;
    this->msgTunic = 0;
    this->retreat = false;
    this->soundTimer = 0;
    this->soundEatCount = 0;
    this->struggleCounter = 0;
    this->invincibilityTimer = 0;
    this->effectTimer = 0;
    this->hasPlayer = false;
    this->stopScroll = false;
    this->ocTimer = 0;
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
    static f32 segmentHeights[] = { 0.0f, 500.0f, 750.0f, 1000.0f, 1250.0f };
    s32 i;
    f32 angleMod;

    this->reachState = 1;
    this->actionTimer = 0;
    this->segPhaseVelTarget = 2500.0f;
    this->segMoveRate = 0.0f;

    if (!this->reachUp) {
        angleMod = 4096.0f + (this->actor.scale.y * 150000.0f);
    } else {
        angleMod = 0.0f;
        this->wobbleSize = 512.0f;
        this->wobbleSizeTarget = 512.0f;
    }

    for (i = 1; i < 5; i++) {
        this->bodySegs[i].heightTarget = segmentHeights[i];
        if (this->reachUp) {
            this->bodySegs[i].heightTarget += (abs(this->actor.yDistToPlayer) * 4) * i;
        }
        this->bodySegs[i].scaleTarget.x = this->bodySegs[i].scaleTarget.z = 0.75f;
        this->bodySegs[i].rotTarget.x = angleMod;
        this->bodySegs[i].rotTarget.z = 0.0f;
    }

    this->actionFunc = EnRr_Reach;
    Audio_PlayActorSound2(&this->actor, NA_SE_EN_LIKE_UNARI);
}

void EnRr_SetupNeutral(EnRr* this) {
    s32 i;

    this->reachState = 0;
    this->segMoveRate = 0.0f;
    this->segPhaseVelTarget = 2500.0f;
    this->wobbleSizeTarget = 2560.0f;
    for (i = 1; i < 5; i++) {
        this->bodySegs[i].heightTarget = 0.0f;
        this->bodySegs[i].rotTarget.x = this->bodySegs[i].rotTarget.z = 0.0f;
        this->bodySegs[i].scaleTarget.x = this->bodySegs[i].scaleTarget.z = 0.875f;
    }

    if (this->retreat) {
        this->actionTimer = 140;
        this->actionFunc = EnRr_Retreat;
    } else {
        this->actionTimer = 50;
        this->actionFunc = EnRr_Approach;
    }
}

void EnRr_SetupGrabPlayer(EnRr* this, Player* player) {
    s32 i;
    
    if (this->segPhaseVel < 1024.0f) {
        this->segPhaseVel = 1024.0f;
        this->soundTimer = 24;
    } else {
        this->soundTimer = 32768.0f / this->segPhaseVel;
    }

    if ((player->actor.velocity.y < -15.0f) && (this->actor.params != RUPEE_LIKE)) {
        this->soundEatCount = 0;
    } else {
        this->soundEatCount = 12;
    }
    this->struggleCounter = 0;

    if (this->actor.params == RUPEE_LIKE) {
        this->grabState = 3;
        this->soundEatCount = 100;
    } else if ((this->actor.params == LIKE_LIKE_SMALL) && (gSaveContext.linkAge == LINK_AGE_ADULT)) {
        this->grabState = 3; 
    } else {
        this->grabState = 1;
    }

    this->grabEject = 0;
    this->actor.flags &= ~ACTOR_FLAG_TARGETABLE;
    this->collider1.base.ocFlags1 &= ~OC1_TYPE_PLAYER;
    this->colPlayerTimer = 20;
    this->actionTimer = 0;
    this->hasPlayer = true;
    this->reachState = 0;
    this->segMoveRate = this->swallowOffset = this->actor.speedXZ = 0.0f;
    if (this->reachUp) {
        this->wobbleSize = 0.0f;
    }
    for (i = 1; i < 5; i++) {
        this->bodySegs[i].heightTarget = 0.0f;
        this->bodySegs[i].rotTarget.x = this->bodySegs[i].rotTarget.z = 0.0f;
        this->bodySegs[i].scaleTarget.x = this->bodySegs[i].scaleTarget.z = 1.0f;
    }
    this->bodySegs[RR_MOUTH].scaleTarget.x = this->bodySegs[RR_MOUTH].scaleTarget.z = 0.875;

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

void EnRr_SetupReleasePlayer(EnRr* this, PlayState* play) {
    Player* player = GET_PLAYER(play);
    f32 launchXZ;
    f32 launchY;

    if (player->stateFlags2 & PLAYER_STATE2_GRABBED_BY_ENEMY) {
        this->actor.flags |= ACTOR_FLAG_TARGETABLE;
        this->hasPlayer = false;
        player->av2.actionVar2 = 100;
        this->actionTimer = 50;
        this->ocTimer = 50;
        this->segMoveRate = 0.0f;
        this->segPhaseVel = 2500.0f;
        this->segPhaseVelTarget = 2500.0f;
        this->wobbleSizeTarget = 2560.0f;
        this->pulseSizeTarget = 0.15f;

        switch (EnRr_GetMessage(this->msgShield, this->msgTunic)) {
            case RR_MESSAGE_SHIELD:
                Message_StartTextbox(play, 0x305F, NULL);
                break;
            case RR_MESSAGE_TUNIC:
                Message_StartTextbox(play, 0x3060, NULL);
                break;
            case RR_MESSAGE_TUNIC | RR_MESSAGE_SHIELD:
                Message_StartTextbox(play, 0x3061, NULL);
                break;
        }
        this->msgShield = 0;
        this->msgTunic = 0;

        player->actor.parent = NULL;

        launchXZ = this->actor.scale.x * 150.0f;
        launchY = this->actor.scale.x * 450.0f;

        player->actor.world.pos.x += launchXZ * Math_SinS(this->actor.shape.rot.y);
        player->actor.world.pos.y += launchY;
        player->actor.world.pos.z += launchXZ * Math_CosS(this->actor.shape.rot.y);

        if (this->actor.params != LIKE_LIKE_INVERT) {
            func_8002F6D4(play, &this->actor, launchXZ, this->actor.shape.rot.y, launchY, 0);
        } else {
            func_8002F6D4(play, &this->actor, 0.0f, this->actor.shape.rot.y, 0.0f, 0);
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
}

void EnRr_SetupDamage(EnRr* this) {
    s32 i;

    this->reachState = 0;
    this->grabState = 0;
    this->actor.speedXZ = 0.0f;
    this->segMoveRate = 0.0f;
    this->segPhaseVel = 500.0f;
    this->segPhaseVelTarget = 2500.0f;
    this->pulseSizeTarget = 0.0f;
    this->wobbleSizeTarget = 0.0f;
    for (i = 1; i < 5; i++) {
        this->bodySegs[i].heightTarget = 0.0f;
        this->bodySegs[i].rotTarget.x = this->bodySegs[i].rotTarget.z = 0.0f;
        this->bodySegs[i].scaleTarget.x = this->bodySegs[i].scaleTarget.z = 0.875f;
    }

    this->actionFunc = EnRr_Damage;
    Audio_PlayActorSound2(&this->actor, NA_SE_EN_LIKE_DAMAGE);
}

void EnRr_SetupThrowPlayer(EnRr* this, PlayState* play) {
    Player* player = GET_PLAYER(play);
    static f32 segmentHeights[] = { 0.0f, 500.0f, 750.0f, 1000.0f, 1250.0f };
    s32 i;

    player->av2.actionVar2 = 0;
    this->reachState = 1;
    this->actionTimer = 10;
    this->segMoveRate = 0.0f;

    for (i = 1; i < 5; i++) {
        this->bodySegs[i].heightTarget = segmentHeights[i];
        if (this->actor.params != LIKE_LIKE_INVERT) {
            this->bodySegs[i].rotTarget.x = 2560.0f;
        } else {
            this->bodySegs[i].rotTarget.x = 0.0f;
        }
        this->bodySegs[i].rotTarget.z = 0.0f;
        this->bodySegs[i].scaleTarget.x = this->bodySegs[i].scaleTarget.z = 0.875f;
    }
    this->bodySegs[RR_MOUTH].scaleTarget.x = this->bodySegs[RR_MOUTH].scaleTarget.z = 0.75f;

    this->actionFunc = EnRr_ThrowPlayer;
}

void EnRr_SetupApproach(EnRr* this) {
    s32 i;

    this->segMoveRate = 0.0f;
    this->pulseSizeTarget = 0.15f;
    this->segPhaseVelTarget = 2500.0f;
    this->wobbleSizeTarget = 2560.0f;
    for (i = 1; i < 5; i++) {
        this->bodySegs[i].heightTarget = 0.0f;
        this->bodySegs[i].rotTarget.x = this->bodySegs[i].rotTarget.z = 0.0f;
        this->bodySegs[i].scaleTarget.x = this->bodySegs[i].scaleTarget.z = 0.875f;
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
    this->actor.speedXZ = 0.0f;
    this->segMovePhase = 0;
    this->segPhaseVel = 0.0f;
    this->segPhaseVelTarget = 2500.0f;
    this->segPulsePhaseDiff = 0.0f;
    this->segWobblePhaseDiffX = 0.0f;
    this->segWobbleXTarget = 3.0f;
    this->segWobblePhaseDiffZ = 0.0f;
    this->segWobbleZTarget = 1.0f;
    this->pulseSize = 0.0f;
    this->pulseSizeTarget = 0.15f;
    this->wobbleSize = 0.0f;
    this->wobbleSizeTarget = 2560.0f;
    for (i = 1; i < 5; i++) {
        this->bodySegs[i].scaleMod.y = 0.0f;
        this->bodySegs[i].rotTarget.x = 0.0f;
        this->bodySegs[i].rotTarget.y = 0.0f;
        this->bodySegs[i].rotTarget.z = 0.0f;
        this->bodySegs[i].scale.x = this->bodySegs[i].scale.y = this->bodySegs[i].scale.z =
            this->bodySegs[i].scaleTarget.x = this->bodySegs[i].scaleTarget.y = this->bodySegs[i].scaleTarget.z = 0.875;
    }
    this->actionFunc = EnRr_Stunned;
}

void EnRr_CollisionCheck(EnRr* this, PlayState* play) {
    Vec3f hitPos;
    Player* player = GET_PLAYER(play);

    if (this->collider2.base.acFlags & AC_HIT) {
        this->collider2.base.acFlags &= ~AC_HIT;

        hitPos.x = this->collider2.info.bumper.hitPos.x;
        hitPos.y = this->collider2.info.bumper.hitPos.y;
        hitPos.z = this->collider2.info.bumper.hitPos.z;
        //CollisionCheck_SpawnShieldParticlesMetal2(play, &hitPos);
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
                        Enemy_StartFinishingBlow(play, this);
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
                case RR_DMG_STUN: // Boomerang
                    Audio_PlayActorSound2(&this->actor, NA_SE_EN_GOMA_JR_FREEZE);
                    Actor_SetColorFilter(&this->actor, 0, 0xFF, 0x2000, 0x50);
                    EnRr_SetupStunned(this);
                    return;
            }
        }
        if ((this->ocTimer == 0) && (this->actor.colorFilterTimer == 0) &&
            (~player->stateFlags2 & PLAYER_STATE2_GRABBED_BY_ENEMY) &&
            (player->invincibilityTimer == 0) &&
            (((this->collider1.base.ocFlags1 & OC1_HIT) || (this->collider2.base.ocFlags1 & OC1_HIT)) && 
            ((this->collider1.base.oc == &player->actor) || (this->collider2.base.oc == &player->actor)))) {
            this->collider1.base.ocFlags1 &= ~OC1_HIT;
            this->collider2.base.ocFlags1 &= ~OC1_HIT;

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
    this->segPhaseVelTarget = 2500.0f;
    this->segPulsePhaseDiff = 0.0f;
    this->segWobblePhaseDiffX = 0.0f;
    this->segWobbleXTarget = 3.0f;
    this->segWobblePhaseDiffZ = 0.0f;
    this->segWobbleZTarget = 1.0f;
    this->pulseSize = 0.0f;
    this->pulseSizeTarget = 0.15f;
    this->wobbleSize = 0.0f;
    this->wobbleSizeTarget = 2560.0f;
    for (i = 0; i < 5; i++) {
        this->bodySegs[i].scaleMod.y = 0.0f;
        this->bodySegs[i].rotTarget.x = 0.0f;
        this->bodySegs[i].rotTarget.y = 0.0f;
        this->bodySegs[i].rotTarget.z = 0.0f;
        this->bodySegs[i].scale.x = this->bodySegs[i].scale.y = this->bodySegs[i].scale.z =
            this->bodySegs[i].scaleTarget.x = this->bodySegs[i].scaleTarget.y = this->bodySegs[i].scaleTarget.z =
                0.875f;
    }
    this->bodySegs[RR_BASE].scaleTarget.x = this->bodySegs[RR_BASE].scaleTarget.y =
        this->bodySegs[RR_BASE].scaleTarget.z = 1.0f;

    for (i = 1; i < 5; i++) {
        this->bodySegs[i].scaleMod.x = this->bodySegs[i].scaleMod.z =
            Math_CosS(i * (u32)(s16)this->segPulsePhaseDiff * 0x1000) * this->pulseSize;
    }
    for (i = 1; i < 5; i++) {
        this->bodySegs[i].rotTarget.x = Math_CosS(i * (u32)(s16)this->segWobblePhaseDiffX * 0x1000) * this->wobbleSize;
        this->bodySegs[i].rotTarget.z = Math_SinS(i * (u32)(s16)this->segWobblePhaseDiffZ * 0x1000) * this->wobbleSize;
    }
}

void EnRr_UpdateBodySegments(EnRr* this, PlayState* play) {
    s32 i;
    s16 phase = this->segMovePhase;

    if (!this->isDead) {
        for (i = 1; i < 5; i++) {
            this->bodySegs[i].scaleMod.x = this->bodySegs[i].scaleMod.z =
                Math_CosS(phase + i * (s16)this->segPulsePhaseDiff * 0x1000) * this->pulseSize;
        }
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

void EnRr_Approach(EnRr* this, PlayState* play) {
    Player* player = GET_PLAYER(play);
    this->actor.world.rot.y = this->actor.shape.rot.y;
    if (this->actor.xyzDistToPlayerSq < SQ(650.0f)) {
        Math_SmoothStepToS(&this->actor.shape.rot.y, this->actor.yawTowardsPlayer, 0xA, 0x400, 0);
    }
    if ((this->actor.speedXZ == 0) && (this->wobbleSizeTarget > 0.0f)) {
        this->wobbleSizeTarget -= 25.6f;
    }

    if ((this->actionTimer == 0) && (((abs(this->actor.yDistToPlayer) < ((f32)this->collider1.dim.height + (this->actor.scale.y * 8571.42857f))) &&
        (this->actor.xzDistToPlayer < ((f32)this->collider1.dim.radius * 1.2f))) || (this->actor.xyzDistToPlayerSq < SQ((s32)this->collider1.dim.radius * 3.5f)))) {
        if (~player->stateFlags2 & PLAYER_STATE2_GRABBED_BY_ENEMY) {
            if (abs(this->actor.yDistToPlayer) > (s32)this->collider1.dim.height * 1.2f) {
                this->reachUp = true;
                EnRr_SetupReach(this);
            } else {
                this->reachUp = false;
                EnRr_SetupReach(this);
            }
        }
    } else if ((this->actor.xyzDistToPlayerSq < SQ(300.0f + this->actor.scale.y * 11538.462f)) &&
               (this->actor.speedXZ == 0.0f)) {
        this->wobbleSizeTarget = 2560.0f;
        EnRr_SetSpeed(this, 2.5f);
    } 
}

void EnRr_Reach(EnRr* this, PlayState* play) {
    Math_SmoothStepToS(&this->actor.shape.rot.y, this->actor.yawTowardsPlayer, 0xA, 0x400, 0);
    this->actor.world.rot.y = this->actor.shape.rot.y;
    switch (this->reachState) {
        case REACH_EXTEND:
            if (this->actionTimer == 0) {
                this->actionTimer = 10;
                this->bodySegs[RR_MOUTH].scaleTarget.x = this->bodySegs[RR_MOUTH].scaleTarget.z = 2.5f;
                this->reachState = REACH_OPEN;
            }
            break;
        case REACH_OPEN:
            if (this->actionTimer == 0) {
                this->actionTimer = 3;
                this->bodySegs[RR_MOUTH].heightTarget = 1750.0f;
                this->reachState = REACH_GAPE;
            }
            break;
        case REACH_GAPE:
            if (this->actionTimer == 0) {
                this->actionTimer = 34;
                this->bodySegs[RR_MOUTH].scaleTarget.x = this->bodySegs[RR_MOUTH].scaleTarget.z = 0.75f;
                this->reachState = REACH_CLOSE;
            }
            break;
        case REACH_CLOSE:
            if (this->actionTimer == 0) {
                EnRr_SetupNeutral(this);
            }
            break;
    }
}

void EnRr_GrabPlayer(EnRr* this, PlayState* play) {
    f32 soundMod;
    f32 decRate;
    f32 decMod;
    f32 phaseVelMod;
    f32 wobbleMod;
    f32 pulseMod;
    s16 pushOut;
    s16 stolenLife;

    Player* player = GET_PLAYER(play);
    player->actor.speedXZ = 0.0f;
    player->actor.velocity.y = this->actor.velocity.y;
    if (player->av2.actionVar2 > 0) {
        this->struggleCounter += player->av2.actionVar2;
        player->av2.actionVar2 = 0;
    }

    if (this->struggleCounter > 0) {
        if (this->actor.params != LIKE_LIKE_GIANT) {
            this->struggleCounter--;
        } else {
            this->struggleCounter -= 2;
        }
    }

    if ((this->actor.params == LIKE_LIKE_SMALL) && (gSaveContext.linkAge == LINK_AGE_ADULT)) {
        this->struggleCounter += 3;
        this->bodySegs[RR_MOUTH].scaleTarget.x = this->bodySegs[RR_MOUTH].scaleTarget.z = 1.0f;
    }

    if (this->actor.xzDistToPlayer > (this->collider1.dim.radius * 1.3f)) {
        this->grabEject++;
        if (this->grabEject > 40) {
            EnRr_SetupThrowPlayer(this, play);
        }
    }

    func_800AA000(this->actor.xyzDistToPlayerSq, 120, 2, 120);

    this->ocTimer = 8;
    soundMod = 32768.0f / this->segPhaseVel;
    this->soundTimer--;
    if (this->soundTimer == 0) {
        if (this->grabState != 3 && (gSaveContext.health > 16 || this->grabState == 2)) {
            play->damagePlayer(play, -1);
            if (this->actor.params == LIFE_LIKE) {
                stolenLife += 1;
                if ((stolenLife == 8) && (this->actor.colChkInfo.health < 12)) {
                    this->actor.colChkInfo.health += 1;
                    stolenLife = 0;
                    Audio_PlaySoundGeneral(NA_SE_SY_HP_RECOVER, &D_801333D4, 4, &D_801333E0, &D_801333E0, &D_801333E8);
                }
            }
        }
        this->soundEatCount--;
        this->soundTimer = (s16)soundMod;
        if (this->actor.params == RUPEE_LIKE && this->soundEatCount % 2 == 0 && gSaveContext.rupees > 0) {
            Rupees_ChangeBy(-5);
            this->eatenRupees += 5; 
            Audio_PlaySoundGeneral(NA_SE_SY_GET_RUPY, &D_801333D4, 4, &D_801333E0, &D_801333E0, &D_801333E8);
        }
        Audio_PlayActorSound2(&this->actor, NA_SE_EN_LIKE_EAT);
    }

    switch (this->grabState) {
        case 1:
            phaseVelMod = (f32)this->struggleCounter * 10.24;
            wobbleMod = (f32)this->struggleCounter * 3.413f;
            pulseMod = (f32)this->struggleCounter / 6000.0f;

            this->segPhaseVelTarget = 4096.0f - phaseVelMod; // Caps at 3072;
            this->wobbleSizeTarget = 512.0f + wobbleMod;     // Caps at 1024
            this->pulseSizeTarget = 0.15f + pulseMod;        // Caps at 0.175

            if (this->soundEatCount == 0) {
                if (this->actor.params != LIFE_LIKE) {

                    if (CUR_EQUIP_VALUE(EQUIP_TYPE_SHIELD) == EQUIP_VALUE_SHIELD_DEKU ||
                        CUR_EQUIP_VALUE(EQUIP_TYPE_SHIELD) == EQUIP_VALUE_SHIELD_HYLIAN ||
                        (CUR_EQUIP_VALUE(EQUIP_TYPE_TUNIC) == EQUIP_VALUE_TUNIC_GORON ||
                         CUR_EQUIP_VALUE(EQUIP_TYPE_TUNIC) == EQUIP_VALUE_TUNIC_ZORA &&
                             !IS_RANDO) /* Rando Save File */) {
                        func_80064520(play, &play->csCtx);
                        Audio_PlaySoundGeneral(NA_SE_VO_LI_DAMAGE_S, &player->actor.projectedPos, 4, &D_801333E0,
                                               &D_801333E0, &D_801333E8);
                        this->soundEatCount = 16;
                        this->segPhaseVel = 5461.33f;
                        this->segPhaseVelTarget = 5461.33f;
                        this->wobbleSizeTarget = 768.0f;
                        this->pulseSizeTarget = 0.15f;
                        this->segWobbleXTarget = 4.0f;
                        this->segWobbleZTarget = 3.0f;
                        this->grabState = 2;
                    } else {
                        this->soundEatCount = 30;
                        this->grabState = 3;
                    }
                } else if (this->actor.params == LIFE_LIKE) {
                    this->soundEatCount = 320;
                    this->segPhaseVel = 5461.33f;
                    this->segPhaseVelTarget = 5461.33f;
                    this->wobbleSizeTarget = 768.0f;
                    this->pulseSizeTarget = 0.15f;
                    this->segWobbleXTarget = 4.0f;
                    this->segWobbleZTarget = 3.0f;
                    this->grabState = 2;
                }
            }
            break;
        case 2: 
            if ((this->soundEatCount == 0) && (this->actor.params != LIFE_LIKE)) {
                this->struggleCounter = 0;
                if (CUR_EQUIP_VALUE(EQUIP_TYPE_SHIELD) == EQUIP_VALUE_SHIELD_DEKU || CUR_EQUIP_VALUE(EQUIP_TYPE_SHIELD) == EQUIP_VALUE_SHIELD_HYLIAN) {
                    this->msgShield = Inventory_DeleteEquipment(play, EQUIP_TYPE_SHIELD);
                    if (this->msgShield != 0) {
                        this->eatenShield = this->msgShield;
                        this->retreat = true;
                        this->actor.colChkInfo.health += 2;
                    }
                }
                if (CUR_EQUIP_VALUE(EQUIP_TYPE_TUNIC) == EQUIP_VALUE_TUNIC_GORON || CUR_EQUIP_VALUE(EQUIP_TYPE_TUNIC) == EQUIP_VALUE_TUNIC_ZORA && !IS_RANDO /* Rando Save File */) {
                    this->msgTunic = Inventory_DeleteEquipment(play, EQUIP_TYPE_TUNIC);
                    if (this->msgTunic != 0) {
                        this->eatenTunic = this->msgTunic;
                        this->retreat = true;
                        this->actor.colChkInfo.health += 2;
                    }
                }
                if (this->msgShield != 0 || this->msgTunic != 0) {
                    Audio_PlayActorSound2(&this->actor, NA_SE_EN_LIKE_DRINK);
                }

                func_80064534(play, &play->csCtx);
                this->soundEatCount = 30;
                this->segPhaseVel = 4096.0f;
                this->segWobbleXTarget = 3.0f;
                this->segWobbleZTarget = 1.0f;
                this->grabState = 3;
                }
            else if ((this->soundEatCount == 0) && (this->actor.params == LIFE_LIKE)) {
                EnRr_SetupThrowPlayer(this, play);
            }
            break;
        case 3:
            phaseVelMod = (f32)this->struggleCounter * 15.36;
            wobbleMod = (f32)this->struggleCounter * 12.80;
            pulseMod = (f32)this->struggleCounter / 3000.0f;
            this->segPhaseVelTarget = 3072.0f - phaseVelMod;
            this->wobbleSizeTarget = 256.0f + wobbleMod;
            this->pulseSizeTarget = 0.15f + pulseMod;
            if (this->soundEatCount == 0) {
                EnRr_SetupThrowPlayer(this, play);
            }
            break;
    }

    if ((this->struggleCounter >= 150) || (~player->stateFlags2 & PLAYER_STATE2_GRABBED_BY_ENEMY)) {
        this->grabState = 0;
        EnRr_SetupThrowPlayer(this, play);
    } else {
        decRate = 1.0f / (((f32)this->collider1.dim.height / 24.0f) * (1.0f - (this->actor.scale.y * 12.5f)));
        decMod =  ((this->actor.xzDistToPlayer / ((f32)this->collider1.dim.radius * 3.0f)) - 1.0f);
        Math_ApproachF(&player->actor.world.pos.x, this->mouthPos.x, 1.0f, 8.0f);
        Math_ApproachF(&player->actor.world.pos.y, this->mouthPos.y + this->swallowOffset, 1.0f, 8.0f);
        Math_ApproachF(&player->actor.world.pos.z, this->mouthPos.z, 1.0f, 8.0f);
        if (this->actor.params != LIKE_LIKE_INVERT) {
            Math_ApproachF(&this->swallowOffset, -(f32)this->collider1.dim.height, 1.0f, pow(decRate, decMod));
        } else {
            Math_ApproachF(&this->swallowOffset, ((f32)player->cylinder.dim.height) - ((f32)this->collider1.dim.height * 0.825f), 1.0f, pow(decRate, decMod));
        }
        
    }
}

void EnRr_ThrowPlayer(EnRr* this, PlayState* play) {
    f32 decRate;

    Player* player = GET_PLAYER(play);
    player->av2.actionVar2 = 0;
    player->actor.speedXZ = 0.0f;
    player->actor.velocity.y = this->actor.velocity.y;

    decRate = ((f32)this->collider1.dim.height / 20.0f) * (1.0f - (this->actor.scale.y * 15.0f));
    Math_ApproachF(&player->actor.world.pos.x, this->mouthPos.x, 1.0f, 8.0f);
    Math_ApproachF(&player->actor.world.pos.y, this->mouthPos.y + this->swallowOffset, 1.0f, 12.0f);
    Math_ApproachF(&player->actor.world.pos.z, this->mouthPos.z, 1.0f, 8.0f);
    if (this->actor.params != LIKE_LIKE_INVERT) {
        Math_ApproachF(&this->swallowOffset, -(f32)this->collider1.dim.height * 0.33f, 1.0f, decRate);
    } else {
        Math_ApproachF(&this->swallowOffset, (f32)player->cylinder.dim.height - (f32)this->collider1.dim.height, 1.0f, decRate);
    }

    if (this->actionTimer == 0) {
        this->reachState = 0;
        EnRr_SetupReleasePlayer(this, play);
        EnRr_SetupNeutral(this);
    }
}

void EnRr_Damage(EnRr* this, PlayState* play) {
    s32 i;

    this->invincibilityTimer--;
    if (this->invincibilityTimer == 0) {
        EnRr_SetupApproach(this);
        return;
    } else if ((this->invincibilityTimer & 8) != 0) {
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
        this->segPhaseVelTarget = 2500.0f;
        this->actionFunc = EnRr_Approach;
    } else {
        Math_SmoothStepToS(&this->actor.shape.rot.y, this->actor.yawTowardsPlayer + 0x8000, 0xA, 0x600, 0);
        this->actor.world.rot.y = this->actor.shape.rot.y;
        this->segPhaseVelTarget = 3200.0f;
        if (this->actor.speedXZ == 0.0f) {
            EnRr_SetSpeed(this, 3.0f);
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

void EnRr_GenerateRipple(EnRr* this, PlayState* play) {
    Vec3f ripplePos;

    if ((this->actor.yDistToWater < this->collider1.dim.height) && (this->actor.yDistToWater > 1.0f) &&
        ((play->gameplayFrames % 9) == 0)) {
        ripplePos.x = this->actor.world.pos.x;
        ripplePos.y = this->actor.world.pos.y + this->actor.yDistToWater;
        ripplePos.z = this->actor.world.pos.z;
        EffectSsGRipple_Spawn(play, &ripplePos, this->actor.scale.x * 34210.527f, this->actor.scale.x * 60526.316f, 0);
    }
}

void EnRr_Update(Actor* thisx, PlayState* play) {
    s32 pad;
    EnRr* this = (EnRr*)thisx;
    s32 i;

    this->frameCount++;
    if (!this->stopScroll) {
        this->scrollTimer++;
        this->scrollMod += (s16)this->segPhaseVel / 650;
    }
    if (this->actionTimer != 0) {
        this->actionTimer--;
    }
    if (this->ocTimer != 0) {
        this->ocTimer--;
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
    if (this->retreat || this->actionFunc == EnRr_Reach || this->actionFunc == EnRr_GrabPlayer) {
        Math_StepToF(&this->actor.speedXZ, 0.0f, 0.15f);
    } else { 
        Math_StepToF(&this->actor.speedXZ, 0.0f, this->segPhaseVel / 25000.0f);
    }
    Actor_MoveForward(&this->actor);
    Collider_UpdateCylinder(&this->actor, &this->collider1);
    this->collider2.dim.pos.x = this->mouthPos.x;
    this->collider2.dim.pos.y = this->mouthPos.y;
    this->collider2.dim.pos.z = this->mouthPos.z;
    if (!this->isDead) {
        CollisionCheck_SetAC(play, &play->colChkCtx, &this->collider1.base);
        CollisionCheck_SetAC(play, &play->colChkCtx, &this->collider2.base);
        CollisionCheck_SetOC(play, &play->colChkCtx, &this->collider1.base);
        CollisionCheck_SetOC(play, &play->colChkCtx, &this->collider2.base);
    } else {
        this->collider1.base.ocFlags1 &= ~OC1_HIT;
        this->collider2.base.ocFlags1 &= ~OC1_HIT;
        this->collider1.base.acFlags &= ~AC_HIT;
        this->collider2.base.acFlags &= ~AC_HIT;
    }
    CollisionCheck_SetOC(play, &play->colChkCtx, &this->collider1.base);

    Actor_UpdateBgCheckInfo(play, &this->actor, 30.0f, this->collider1.dim.radius, this->collider1.dim.height, 5);
    EnRr_GenerateRipple(this, play);

    if (this->colPlayerTimer > 0) {
        Player* player = GET_PLAYER(play);

        if (~player->stateFlags2 & PLAYER_STATE2_GRABBED_BY_ENEMY) {
            this->colPlayerTimer--;
            if (this->colPlayerTimer == 0) {
                this->collider1.base.ocFlags1 |= OC1_TYPE_PLAYER;
            }
        }
    }

    if (!this->stopScroll) {
        Math_ApproachF(&this->segPhaseVel, this->segPhaseVelTarget, 1.0f, 25.0f);
        Math_ApproachF(&this->segPulsePhaseDiff, 4.0f, 1.0f, 5.0f);
        Math_ApproachF(&this->segWobblePhaseDiffX, this->segWobbleXTarget, 1.0f, 4.0f);
        Math_ApproachF(&this->segWobblePhaseDiffZ, this->segWobbleZTarget, 1.0f, 3.0f);
        Math_ApproachF(&this->pulseSize, this->pulseSizeTarget, 1.0f, 0.0015f);
        Math_ApproachF(&this->wobbleSize, this->wobbleSizeTarget, 1.0f, 25.6f);
        for (i = 0; i < 5; i++) {
            Math_SmoothStepToS(&this->bodySegs[i].rot.x, this->bodySegs[i].rotTarget.x, 5, this->segMoveRate * 384.0f,
                               0);
            Math_SmoothStepToS(&this->bodySegs[i].rot.z, this->bodySegs[i].rotTarget.z, 5, this->segMoveRate * 1024.0f,
                               0);
            Math_ApproachF(&this->bodySegs[i].scale.x, this->bodySegs[i].scaleTarget.x, 1.0f,
                           this->segMoveRate * 0.15f);
            this->bodySegs[i].scale.z = this->bodySegs[i].scale.x;
            Math_ApproachF(&this->bodySegs[i].height, this->bodySegs[i].heightTarget, 1.0f,
                           this->segMoveRate * 100.0f);
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
               Gfx_TwoTexScroll(play->state.gfxCtx, 0, (this->scrollTimer * 0) & 0x7F, (this->scrollTimer * 0) & 0x3F,
                                32, 16, 1, (this->scrollTimer * 0) & 0x3F,
                                (-this->scrollTimer - this->scrollMod) & 0x7F, 32, 16));

    if (this->actor.params == RUPEE_LIKE) {
        if (this->eatenRupees < 50) {
            Gfx_SetFog2(POLY_OPA_DISP, 0, 255, 0, 0, 0, 4000);
        } else if ((this->eatenRupees >= 25) && (this->eatenRupees < 50)) {
            Gfx_SetFog2(POLY_OPA_DISP, 0, 0, 255, 0, 0, 4000);
        } else if ((this->eatenRupees >= 50) && (this->eatenRupees < 100)) {
            Gfx_SetFog2(POLY_OPA_DISP, 255, 0, 0, 0, 0, 4000);
        } else if (this->eatenRupees >= 100) {
        Gfx_SetFog2(POLY_OPA_DISP, 160, 0, 160, 0, 0, 4000);
        }
    } else if (this->actor.params == LIFE_LIKE) {
        Gfx_SetFog2(POLY_OPA_DISP, 255, 0, 0, 0, 0, 4000 - (s32)(Math_CosS(this->scrollTimer * 256) * 2000.0f));
    }


    Matrix_Push();

    Matrix_Scale((1.0f + this->bodySegs[RR_BASE].scaleMod.x) * this->bodySegs[RR_BASE].scale.x,
                 (1.0f + this->bodySegs[RR_BASE].scaleMod.y) * this->bodySegs[RR_BASE].scale.y,
                 (1.0f + this->bodySegs[RR_BASE].scaleMod.z) * this->bodySegs[RR_BASE].scale.z, MTXMODE_APPLY);
    gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
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
                EffectSsEnIce_SpawnFlyingVec3f(play, &this->actor, &effectPos, 150, 150, 150, 250, 235, 245, 255, 3.0f);
            }
        }
    }
}
