/*
 * File: z_obj_syokudai.c
 * Overlay: ovl_Obj_Syokudai
 * Description: Torch
 */

#include "z_obj_syokudai.h"
#include "overlays/actors/ovl_En_Arrow/z_en_arrow.h"
#include "objects/gameplay_keep/gameplay_keep.h"
#include "objects/object_syokudai/object_syokudai.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"

#define FLAGS (ACTOR_FLAG_UPDATE_CULLING_DISABLED | ACTOR_FLAG_HOOKSHOT_PULLS_PLAYER)

void ObjSyokudai_Init(Actor* thisx, PlayState* play);
void ObjSyokudai_Destroy(Actor* thisx, PlayState* play);
void ObjSyokudai_Update(Actor* thisx, PlayState* play);
void ObjSyokudai_Draw(Actor* thisx, PlayState* play);

const ActorInit Obj_Syokudai_InitVars = {
    ACTOR_OBJ_SYOKUDAI,
    ACTORCAT_PROP,
    FLAGS,
    OBJECT_SYOKUDAI,
    sizeof(ObjSyokudai),
    (ActorFunc)ObjSyokudai_Init,
    (ActorFunc)ObjSyokudai_Destroy,
    (ActorFunc)ObjSyokudai_Update,
    (ActorFunc)ObjSyokudai_Draw,
    NULL,
};

static ColliderCylinderInit sCylInitStand = {
    {
        COLTYPE_METAL,
        AT_NONE,
        AC_ON | AC_HARD | AC_TYPE_PLAYER,
        OC1_ON | OC1_TYPE_ALL,
        OC2_TYPE_2,
        COLSHAPE_CYLINDER,
    },
    {
        ELEMTYPE_UNK2,
        { 0x00100000, 0x00, 0x00 },
        { 0xEE01FFFF, 0x00, 0x00 },
        TOUCH_NONE,
        BUMP_ON | BUMP_HOOKABLE,
        OCELEM_ON,
    },
    { 12, 45, 0, { 0, 0, 0 } },
};

static ColliderCylinderInit sCylInitFlame = {
    {
        COLTYPE_NONE,
        AT_NONE,
        AC_ON | AC_TYPE_PLAYER,
        OC1_NONE,
        OC2_NONE,
        COLSHAPE_CYLINDER,
    },
    {
        ELEMTYPE_UNK2,
        { 0x00000000, 0x00, 0x00 },
        { 0x00020820, 0x00, 0x00 },
        TOUCH_NONE,
        BUMP_ON,
        OCELEM_NONE,
    },
    { 15, 45, 45, { 0, 0, 0 } },
};

static ColliderJntSphElementInit sFlameElementsInit[1] = { 
    {
        {
        ELEMTYPE_UNK2,
        { 0x00000000, 0x00, 0x00 },
        { 0x00020820, 0x00, 0x00 },
        TOUCH_NONE,
        BUMP_ON,
        OCELEM_NONE,
        },
        { 1, { { 0, 0, 0 }, 22 }, 100 },
    },
};

static ColliderJntSphInit sJntSphInitFlame = {
    {
        COLTYPE_NONE,
        AT_NONE,
        AC_ON | AC_TYPE_PLAYER,
        OC1_NONE,
        OC2_NONE,
        COLSHAPE_JNTSPH,
    },
    1,
    sFlameElementsInit,
};

static InitChainEntry sInitChain[] = {
    ICHAIN_VEC3F_DIV1000(scale, 1000, ICHAIN_CONTINUE),
    ICHAIN_F32(uncullZoneForward, 4000, ICHAIN_CONTINUE),
    ICHAIN_F32(uncullZoneScale, 800, ICHAIN_CONTINUE),
    ICHAIN_F32(uncullZoneDownward, 800, ICHAIN_STOP),
};

static s32 sLitTorchCount;

void ObjSyokudai_Init(Actor* thisx, PlayState* play) {
    static u8 sColTypesStand[] = { 0x09, 0x0B, 0x0B };
    s32 pad;
    ObjSyokudai* this = (ObjSyokudai*)thisx;
    s32 torchType = this->actor.params & 0xF000;

    Actor_ProcessInitChain(&this->actor, sInitChain);
    ActorShape_Init(&this->actor.shape, 0.0f, NULL, 0.0f);

    Collider_InitCylinder(play, &this->colliderStand);
    Collider_SetCylinder(play, &this->colliderStand, &this->actor, &sCylInitStand);
    this->colliderStand.base.colType = sColTypesStand[this->actor.params >> 0xC];

    //Collider_InitCylinder(play, &this->colliderFlame);
    //Collider_SetCylinder(play, &this->colliderFlame, &this->actor, &sCylInitFlame);

    Collider_InitJntSph(play, &this->colliderFlameSph);
    Collider_SetJntSph(play, &this->colliderFlameSph, &this->actor, &sJntSphInitFlame, this->flameSphItems);
    this->colliderFlameSph.elements[0].dim.worldSphere.radius = sJntSphInitFlame.elements[0].dim.modelSphere.radius;

    this->actor.colChkInfo.mass = MASS_IMMOVABLE;

    Vec3f localLightCenter = { 0.0f, 70.0f, 0.0f };
    Vec3f worldLightCenter;

    // Calculate the tilted position for the light glow
    Matrix_Translate(this->actor.world.pos.x, this->actor.world.pos.y, this->actor.world.pos.z, MTXMODE_NEW);
    Matrix_RotateZYX(this->actor.shape.rot.x, this->actor.shape.rot.y, this->actor.shape.rot.z, MTXMODE_APPLY);
    Matrix_MultVec3f(&localLightCenter, &worldLightCenter);

    // Set the light info using the calculated world coordinates
    Lights_PointGlowSetInfo(&this->lightInfo, worldLightCenter.x, worldLightCenter.y,
                            worldLightCenter.z, 255, 255, 180, -1);
    this->lightNode = LightContext_InsertLight(play, &play->lightCtx, &this->lightInfo);

    if ((this->actor.params & 0x400) || ((torchType != 2) && Flags_GetSwitch(play, this->actor.params & 0x3F))) {
        this->litTimer = -1;
    }

    this->flameTexScroll = (s32)(Rand_ZeroOne() * 20.0f);
    sLitTorchCount = 0;
    Actor_SetFocus(&this->actor, 60.0f);
}

void ObjSyokudai_Destroy(Actor* thisx, PlayState* play) {
    s32 pad;
    ObjSyokudai* this = (ObjSyokudai*)thisx;

    Collider_DestroyCylinder(play, &this->colliderStand);
    //Collider_DestroyCylinder(play, &this->colliderFlame);
    Collider_DestroyJntSph(play, &this->colliderFlameSph);
    LightContext_RemoveLight(play, &play->lightCtx, this->lightNode);
}

void ObjSyokudai_Update(Actor* thisx, PlayState* play2) {
    PlayState* play = play2;
    ObjSyokudai* this = (ObjSyokudai*)thisx;
    s32 torchCount = (this->actor.params >> 6) & 0xF;
    s32 switchFlag = this->actor.params & 0x3F;
    s32 torchType = this->actor.params & 0xF000;
    s32 litTimeScale;
    WaterBox* dummy;
    f32 waterSurface;
    s32 lightRadius = -1;
    u8 brightness = 0;
    Player* player;
    EnArrow* arrow;
    s32 interactionType;
    u32 dmgFlags;
    Vec3f tipToFlame;
    s32 pad;
    s32 pad2;

    litTimeScale = torchCount;
    if (torchCount == 10) {
        torchCount = 24;
    }
    if (WaterBox_GetSurfaceImpl(play, &play->colCtx, this->actor.world.pos.x, this->actor.world.pos.z, &waterSurface,
                                &dummy) &&
        ((waterSurface - this->actor.world.pos.y) > 52.0f)) {
        this->litTimer = 0;
        if (torchType == 1) {
            Flags_UnsetSwitch(play, switchFlag);
            if (torchCount != 0) {
                this->litTimer = 1;
            }
        }
    } else {
        player = GET_PLAYER(play);
        interactionType = 0;
        if (this->actor.params & 0x400) {
            this->litTimer = -1;
        }
        if (torchCount != 0) {
            if (Flags_GetSwitch(play, switchFlag)) {
                if (this->litTimer == 0) {
                    this->litTimer = -1;
                    if (torchType == 0) {
                        OnePointCutscene_Attention(play, &this->actor);
                    }
                } else if (this->litTimer > 0) {
                    this->litTimer = -1;
                }
            } else if (this->litTimer < 0) {
                this->litTimer = 20;
            }
        }
        // if (this->colliderFlame.base.acFlags & AC_HIT) {
        //     dmgFlags = this->colliderFlame.info.acHitInfo->toucher.dmgFlags;
        //     if (dmgFlags & 0x20820) {
        //         interactionType = 1;
        //     }
        if (this->colliderFlameSph.base.acFlags & AC_HIT) {
            dmgFlags = this->colliderFlameSph.elements[0].info.acHitInfo->toucher.dmgFlags;
            if (dmgFlags & 0x20820) {
                interactionType = 1;
            }
        } else if (player->heldItemAction == PLAYER_IA_DEKU_STICK) {
            Math_Vec3f_Diff(&player->meleeWeaponInfo[0].tip, &this->actor.world.pos, &tipToFlame);
            tipToFlame.y -= 67.0f;
            if ((SQ(tipToFlame.x) + SQ(tipToFlame.y) + SQ(tipToFlame.z)) < SQ(20.0f)) {
                interactionType = -1;
            }
        }
        if (interactionType != 0) {
            if (this->litTimer != 0) {
                if (interactionType < 0) {
                    if (player->unk_860 == 0) {
                        player->unk_860 = 210;
                        Audio_PlaySoundGeneral(NA_SE_EV_FLAME_IGNITION, &this->actor.projectedPos, 4,
                                               &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale,
                                               &gSfxDefaultReverb);
                    } else if (player->unk_860 < 200) {
                        player->unk_860 = 200;
                    }
                } else if (dmgFlags & 0x20) {
                    arrow = (EnArrow*)this->colliderFlameSph.base.ac;
                    if ((arrow->actor.update != NULL) && (arrow->actor.id == ACTOR_EN_ARROW)) {
                        arrow->actor.params = 0;
                        arrow->collider.info.toucher.dmgFlags = 0x800;
                    }
                }
                if ((0 <= this->litTimer) && (this->litTimer < (50 * litTimeScale + 100)) && (torchType != 0)) {
                    this->litTimer = 50 * litTimeScale + 100;
                }
            } else if ((torchType != 0) && (((interactionType > 0) && (dmgFlags & 0x20800)) ||
                                            ((interactionType < 0) && (player->unk_860 != 0)))) {

                if ((interactionType < 0) && (player->unk_860 < 200)) {
                    player->unk_860 = 200;
                }
                if (torchCount == 0) {
                    this->litTimer = -1;
                    if (torchType != 2) {
                        Flags_SetSwitch(play, switchFlag);
                        OnePointCutscene_Attention(play, &this->actor);
                    }
                } else {
                    sLitTorchCount++;
                    if (sLitTorchCount >= torchCount) {
                        Flags_SetSwitch(play, switchFlag);
                        OnePointCutscene_Attention(play, &this->actor);
                        this->litTimer = -1;
                    } else {
                        this->litTimer = (litTimeScale * 50) + 110;
                    }
                }
                Audio_PlaySoundGeneral(NA_SE_EV_FLAME_IGNITION, &this->actor.projectedPos, 4,
                                       &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
            }
        }
    }

    Collider_UpdateCylinder(&this->actor, &this->colliderStand);
    CollisionCheck_SetOC(play, &play->colChkCtx, &this->colliderStand.base);
    CollisionCheck_SetAC(play, &play->colChkCtx, &this->colliderStand.base);

    //Collider_UpdateCylinder(&this->actor, &this->colliderFlame);
    //CollisionCheck_SetAC(play, &play->colChkCtx, &this->colliderFlame.base);

    // Manually calculate the flame's tilted position
    Vec3f localFlameCenter = { 0.0f, 67.0f, 0.0f }; 
    Vec3f worldFlameCenter;

    // 1. Replicate the actor's base matrix state (Position + Rotation)
    Matrix_Translate(this->actor.world.pos.x, this->actor.world.pos.y, this->actor.world.pos.z, MTXMODE_NEW);
    Matrix_RotateZYX(this->actor.shape.rot.x, this->actor.shape.rot.y, this->actor.shape.rot.z, MTXMODE_APPLY);
    
    // 2. Apply our local offset to get the exact world position
    Matrix_MultVec3f(&localFlameCenter, &worldFlameCenter);

    // 3. Manually update the joint sphere's world coordinates
    this->colliderFlameSph.elements[0].dim.worldSphere.center.x = worldFlameCenter.x;
    this->colliderFlameSph.elements[0].dim.worldSphere.center.y = worldFlameCenter.y;
    this->colliderFlameSph.elements[0].dim.worldSphere.center.z = worldFlameCenter.z;

    // 4. Register the AC collision check
    CollisionCheck_SetAC(play, &play->colChkCtx, &this->colliderFlameSph.base);

    if (GameInteractor_Should(VB_SWITCH_TIMER_TICK, this->litTimer > 0, this, &this->litTimer)) {
        this->litTimer--;
        if ((this->litTimer == 0) && (torchType != 0)) {
            sLitTorchCount--;
        }
    }
    if (this->litTimer != 0) {
        if ((this->litTimer < 0) || (this->litTimer >= 20)) {
            lightRadius = 200;
        } else {
            lightRadius = (this->litTimer * 200.0f) / 20.0f;
        }
        brightness = (u8)(Rand_ZeroOne() * 127.0f) + 128;
        func_8002F974(&this->actor, NA_SE_EV_TORCH - SFX_FLAG);
    }
    Lights_PointSetColorAndRadius(&this->lightInfo, brightness, brightness, 0, lightRadius);
    this->flameTexScroll++;
}

void ObjSyokudai_Draw(Actor* thisx, PlayState* play) {
    static Gfx* displayLists[] = { gGoldenTorchDL, gTimedTorchDL, gWoodenTorchDL };
    s32 pad;
    ObjSyokudai* this = (ObjSyokudai*)thisx;
    s32 timerMax;

    timerMax = (((this->actor.params >> 6) & 0xF) * 50) + 100;

    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Opa(play->state.gfxCtx);

    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);

    gSPDisplayList(POLY_OPA_DISP++, displayLists[(u16)this->actor.params >> 0xC]);

    if (this->litTimer != 0) {
        f32 flameScale = 1.0f;

        if (this->litTimer > timerMax) {
            flameScale = (timerMax - this->litTimer + 10) / 10.0f;
        } else if ((this->litTimer > 0) && (this->litTimer < 20)) {
            flameScale = this->litTimer / 20.0f;
        }
        flameScale *= 0.0027f;

        Gfx_SetupDL_25Xlu(play->state.gfxCtx);

        gSPSegment(POLY_XLU_DISP++, 0x08,
                   Gfx_TwoTexScroll(play->state.gfxCtx, 0, 0, 0, 0x20, 0x40, 1, 0, (this->flameTexScroll * -20) & 0x1FF,
                                    0x20, 0x80));

        gDPSetPrimColor(POLY_XLU_DISP++, 0x80, 0x80, 255, 255, 0, 255);

        gDPSetEnvColor(POLY_XLU_DISP++, 255, 0, 0, 0);

        Matrix_Translate(0.0f, 52.0f, 0.0f, MTXMODE_APPLY);
        Matrix_RotateY((s16)(Camera_GetCamDirYaw(GET_ACTIVE_CAM(play)) - this->actor.shape.rot.y + 0x8000) *
                           (M_PI / 0x8000),
                       MTXMODE_APPLY);
        Matrix_Scale(flameScale, flameScale, flameScale, MTXMODE_APPLY);

        gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);

        gSPDisplayList(POLY_XLU_DISP++, gEffFire1DL);
    }
    CLOSE_DISPS(play->state.gfxCtx);
}
