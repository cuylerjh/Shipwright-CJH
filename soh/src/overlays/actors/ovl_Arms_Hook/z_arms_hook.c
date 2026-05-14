#include "z_arms_hook.h"
#include "objects/object_link_boy/object_link_boy.h"

#define FLAGS (ACTOR_FLAG_UPDATE_CULLING_DISABLED | ACTOR_FLAG_DRAW_CULLING_DISABLED)

extern Vec3f gHookshotReticleTarget;
extern u8 gHookshotHasReticleTarget;

void ArmsHook_Init(Actor* thisx, PlayState* play);
void ArmsHook_Destroy(Actor* thisx, PlayState* play);
void ArmsHook_Update(Actor* thisx, PlayState* play);
void ArmsHook_Draw(Actor* thisx, PlayState* play);

void ArmsHook_Wait(ArmsHook* this, PlayState* play);
void ArmsHook_Shoot(ArmsHook* this, PlayState* play);

const ActorInit Arms_Hook_InitVars = {
    ACTOR_ARMS_HOOK,
    ACTORCAT_ITEMACTION,
    FLAGS,
    OBJECT_LINK_BOY,
    sizeof(ArmsHook),
    (ActorFunc)ArmsHook_Init,
    (ActorFunc)ArmsHook_Destroy,
    (ActorFunc)ArmsHook_Update,
    (ActorFunc)ArmsHook_Draw,
    NULL,
};

static ColliderQuadInit sQuadInit = {
    {
        COLTYPE_NONE,
        AT_ON | AT_TYPE_PLAYER,
        AC_NONE,
        OC1_NONE,
        OC2_TYPE_PLAYER,
        COLSHAPE_QUAD,
    },
    {
        ELEMTYPE_UNK2,
        { 0x00000080, 0x00, 0x01 },
        { 0xFFCFFFFF, 0x00, 0x00 },
        TOUCH_ON | TOUCH_NEAREST | TOUCH_SFX_NORMAL,
        BUMP_NONE,
        OCELEM_NONE,
    },
    { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
};

static Vec3f sUnusedVec1 = { 0.0f, 0.5f, 0.0f };
static Vec3f sUnusedVec2 = { 0.0f, 0.5f, 0.0f };

static Color_RGB8 sUnusedColors[] = {
    { 255, 255, 100 },
    { 255, 255, 50 },
};

// 24-Vertex Enclosed 3D Chain Link (Chamfered Rectangle)
static Vtx sCustom3DChainLinkVtx[24] = {
    // FRONT FACE -> Base Metal
    { { {  -7,  4,   0 }, 0, { 0, 0 }, { 130, 130, 130, 255 } } },
    { { {   7,  4,   0 }, 0, { 0, 0 }, { 130, 130, 130, 255 } } },
    // CHAMFERS -> Bright Highlights (Simulating edge wear / reflection)
    { { {  15,  4,   8 }, 0, { 0, 0 }, { 255, 255, 255, 255 } } }, 
    { { {  15,  4,  92 }, 0, { 0, 0 }, { 255, 255, 255, 255 } } },
    { { {   7,  4, 100 }, 0, { 0, 0 }, { 130, 130, 130, 255 } } },
    { { {  -7,  4, 100 }, 0, { 0, 0 }, { 130, 130, 130, 255 } } },
    { { { -15,  4,  92 }, 0, { 0, 0 }, { 255, 255, 255, 255 } } },
    { { { -15,  4,   8 }, 0, { 0, 0 }, { 255, 255, 255, 255 } } },
    // INNER HOLE FRONT -> Dark Shadow (Z values pulled in to thicken the ends)
    { { {  -7,  4,  15 }, 0, { 0, 0 }, {  40,  40,  40, 255 } } }, 
    { { {   7,  4,  15 }, 0, { 0, 0 }, {  40,  40,  40, 255 } } },
    { { {   7,  4,  85 }, 0, { 0, 0 }, {  40,  40,  40, 255 } } },
    { { {  -7,  4,  85 }, 0, { 0, 0 }, {  40,  40,  40, 255 } } },

    // BACK FACE -> Ambient Shadow
    { { {  -7, -4,   0 }, 0, { 0, 0 }, {  60,  60,  60, 255 } } },
    { { {   7, -4,   0 }, 0, { 0, 0 }, {  60,  60,  60, 255 } } },
    { { {  15, -4,   8 }, 0, { 0, 0 }, {  90,  90,  90, 255 } } },
    { { {  15, -4,  92 }, 0, { 0, 0 }, {  90,  90,  90, 255 } } },
    { { {   7, -4, 100 }, 0, { 0, 0 }, {  60,  60,  60, 255 } } },
    { { {  -7, -4, 100 }, 0, { 0, 0 }, {  60,  60,  60, 255 } } },
    { { { -15, -4,  92 }, 0, { 0, 0 }, {  90,  90,  90, 255 } } },
    { { { -15, -4,   8 }, 0, { 0, 0 }, {  90,  90,  90, 255 } } },
    // INNER HOLE BACK -> Pitch Black
    { { {  -7, -4,  15 }, 0, { 0, 0 }, {  20,  20,  20, 255 } } },
    { { {   7, -4,  15 }, 0, { 0, 0 }, {  20,  20,  20, 255 } } },
    { { {   7, -4,  85 }, 0, { 0, 0 }, {  20,  20,  20, 255 } } },
    { { {  -7, -4,  85 }, 0, { 0, 0 }, {  20,  20,  20, 255 } } },
};

static Vec3f D_80865B70 = { 0.0f, 0.0f, 0.0f };
static Vec3f D_80865B7C = { 0.0f, 0.0f, 900.0f };
static Vec3f D_80865B88 = { 0.0f, 500.0f, -5000.0f };
static Vec3f D_80865B94 = { 0.0f, -500.0f, -5000.0f };
static Vec3f D_80865BA0 = { 0.0f, 500.0f, 1200.0f };
static Vec3f D_80865BAC = { 0.0f, -500.0f, 1200.0f };

void ArmsHook_SetupAction(ArmsHook* this, ArmsHookActionFunc actionFunc) {
    this->actionFunc = actionFunc;
}

void ArmsHook_Init(Actor* thisx, PlayState* play) {
    ArmsHook* this = (ArmsHook*)thisx;
    Player* player = GET_PLAYER(play);

    Collider_InitQuad(play, &this->collider);
    Collider_SetQuad(play, &this->collider, &this->actor, &sQuadInit);
    ArmsHook_SetupAction(this, ArmsHook_Wait);
    this->unk_1E8 = this->actor.world.pos;
}

void ArmsHook_Destroy(Actor* thisx, PlayState* play) {
    ArmsHook* this = (ArmsHook*)thisx;

    if (this->grabbed != NULL) {
        this->grabbed->flags &= ~ACTOR_FLAG_HOOKSHOT_ATTACHED;
    }
    Collider_DestroyQuad(play, &this->collider);
}

void ArmsHook_Wait(ArmsHook* this, PlayState* play) {
    if (this->actor.parent == NULL) {
        Player* player = GET_PLAYER(play);
        // get correct timer length for hookshot or longshot
        s32 length = ((player->heldItemAction == PLAYER_IA_HOOKSHOT) ? 13 : 20) *
                     CVarGetFloat(CVAR_CHEAT("HookshotReachMultiplier"), 1.0f);

        ArmsHook_SetupAction(this, ArmsHook_Shoot);
        this->speed = player->heldItemAction == PLAYER_IA_HOOKSHOT ? 20.0f : 26.0f;
        Actor_SetProjectileSpeed(&this->actor, this->speed);
        this->actor.parent = &GET_PLAYER(play)->actor;
        this->timer = length;
    }
}

void func_80865044(ArmsHook* this) {
    this->actor.child = this->actor.parent;
    this->actor.parent->parent = &this->actor;
}

s32 ArmsHook_AttachToPlayer(ArmsHook* this, Player* player) {
    player->actor.child = &this->actor;
    player->heldActor = &this->actor;
    if (this->actor.child != NULL) {
        player->actor.parent = NULL;
        this->actor.child = NULL;
        return true;
    }
    return false;
}

void ArmsHook_DetachHookFromActor(ArmsHook* this) {
    if (this->grabbed != NULL) {
        this->grabbed->flags &= ~ACTOR_FLAG_HOOKSHOT_ATTACHED;
        this->grabbed = NULL;
    }
}

s32 ArmsHook_CheckForCancel(ArmsHook* this) {
    Player* player = (Player*)this->actor.parent;

    if (Player_HoldsHookshot(player)) {
        if ((player->itemAction != player->heldItemAction) || (player->actor.flags & ACTOR_FLAG_TALK) ||
            ((player->stateFlags1 & (PLAYER_STATE1_DEAD | PLAYER_STATE1_DAMAGED)))) {
            this->timer = 0;
            ArmsHook_DetachHookFromActor(this);
            Math_Vec3f_Copy(&this->actor.world.pos, &player->unk_3C8);
            return 1;
        }
    }
    return 0;
}

void ArmsHook_AttachHookToActor(ArmsHook* this, Actor* actor) {
    actor->flags |= ACTOR_FLAG_HOOKSHOT_ATTACHED;
    this->grabbed = actor;
    Math_Vec3f_Diff(&actor->world.pos, &this->actor.world.pos, &this->grabbedDistDiff);
}

void ArmsHook_Shoot(ArmsHook* this, PlayState* play) {
    Player* player = GET_PLAYER(play);
    Actor* touchedActor;
    Actor* grabbed;
    Vec3f bodyDistDiffVec;
    Vec3f newPos;
    f32 bodyDistDiff;
    f32 phi_f16;
    DynaPolyActor* dynaPolyActor;
    f32 sp94;
    f32 sp90;
    s32 pad;
    CollisionPoly* poly;
    s32 bgId;
    Vec3f sp78;
    Vec3f prevFrameDiff;
    Vec3f sp60;
    f32 sp5C;
    f32 sp58;
    f32 velocity;
    s32 pad1;

    if ((this->actor.parent == NULL) || (!Player_HoldsHookshot(player))) {
        ArmsHook_DetachHookFromActor(this);
        Actor_Kill(&this->actor);
        return;
    }

    func_8002F8F0(&player->actor, NA_SE_IT_HOOKSHOT_CHAIN - SFX_FLAG);
    ArmsHook_CheckForCancel(this);

    if ((this->timer != 0) && (this->collider.base.atFlags & AT_HIT) &&
        (this->collider.info.atHitInfo->elemType != ELEMTYPE_UNK4)) {
        touchedActor = this->collider.base.at;
        if ((touchedActor->update != NULL) &&
            (touchedActor->flags & (ACTOR_FLAG_HOOKSHOT_PULLS_ACTOR | ACTOR_FLAG_HOOKSHOT_PULLS_PLAYER))) {
            if (this->collider.info.atHitInfo->bumperFlags & BUMP_HOOKABLE) {
                ArmsHook_AttachHookToActor(this, touchedActor);
                if (CHECK_FLAG_ALL(touchedActor->flags, ACTOR_FLAG_HOOKSHOT_PULLS_PLAYER)) {
                    func_80865044(this);
                }
            }
        }
        this->timer = 0;
        Audio_PlaySoundGeneral(NA_SE_IT_ARROW_STICK_CRE, &this->actor.projectedPos, 4, &gSfxDefaultFreqAndVolScale,
                               &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
    } else if (DECR(this->timer) == 0) {
        grabbed = this->grabbed;
        if (grabbed != NULL) {
            if ((grabbed->update == NULL) || !CHECK_FLAG_ALL(grabbed->flags, ACTOR_FLAG_HOOKSHOT_ATTACHED)) {
                grabbed = NULL;
                this->grabbed = NULL;
            } else if (this->actor.child != NULL) {
                sp94 = Actor_WorldDistXYZToActor(&this->actor, grabbed);
                sp90 = sqrtf(SQ(this->grabbedDistDiff.x) + SQ(this->grabbedDistDiff.y) + SQ(this->grabbedDistDiff.z));
                Math_Vec3f_Diff(&grabbed->world.pos, &this->grabbedDistDiff, &this->actor.world.pos);
                if (50.0f < (sp94 - sp90)) {
                    ArmsHook_DetachHookFromActor(this);
                    grabbed = NULL;
                }
            }
        }

        bodyDistDiff = Math_Vec3f_DistXYZAndStoreDiff(&player->unk_3C8, &this->actor.world.pos, &bodyDistDiffVec);
        if (bodyDistDiff < this->speed) {
            velocity = 0.0f;
            phi_f16 = 0.0f;
        } else {
            if (this->actor.child != NULL) {
                velocity = player->heldItemAction == PLAYER_IA_HOOKSHOT ? 30.0f : 45.0f;
            } else if (grabbed != NULL) {
                velocity = 50.0f;
            } else {
                velocity = this->speed * 5.0f;
            }
            phi_f16 = bodyDistDiff - velocity;
            if (bodyDistDiff <= velocity) {
                phi_f16 = 0.0f;
            }
            velocity = phi_f16 / bodyDistDiff;
        }

        newPos.x = bodyDistDiffVec.x * velocity;
        newPos.y = bodyDistDiffVec.y * velocity;
        newPos.z = bodyDistDiffVec.z * velocity;

        if (this->actor.child == NULL) {
            if ((grabbed != NULL) && (grabbed->id == ACTOR_BG_SPOT06_OBJECTS)) {
                Math_Vec3f_Diff(&grabbed->world.pos, &this->grabbedDistDiff, &this->actor.world.pos);
                phi_f16 = 1.0f;
            } else {
                Math_Vec3f_Sum(&player->unk_3C8, &newPos, &this->actor.world.pos);
                if (grabbed != NULL) {
                    Math_Vec3f_Sum(&this->actor.world.pos, &this->grabbedDistDiff, &grabbed->world.pos);
                }
            }
        } else {
            Math_Vec3f_Diff(&bodyDistDiffVec, &newPos, &player->actor.velocity);
            player->actor.world.rot.x =
                Math_Atan2S(sqrtf(SQ(bodyDistDiffVec.x) + SQ(bodyDistDiffVec.z)), -bodyDistDiffVec.y);
        }

        if (phi_f16 < 50.0f) {
            ArmsHook_DetachHookFromActor(this);
            if (phi_f16 == 0.0f) {
                ArmsHook_SetupAction(this, ArmsHook_Wait);
                if (ArmsHook_AttachToPlayer(this, player)) {
                    Math_Vec3f_Diff(&this->actor.world.pos, &player->actor.world.pos, &player->actor.velocity);
                    player->actor.velocity.y -= 20.0f;
                }
            }
        }
    } else {
        Actor_MoveXZGravity(&this->actor);
        Math_Vec3f_Diff(&this->actor.world.pos, &this->actor.prevPos, &prevFrameDiff);
        Math_Vec3f_Sum(&this->unk_1E8, &prevFrameDiff, &this->unk_1E8);
        this->actor.shape.rot.x = Math_Atan2S(this->actor.speedXZ, -this->actor.velocity.y);
        sp60.x = this->unk_1F4.x - (this->unk_1E8.x - this->unk_1F4.x);
        sp60.y = this->unk_1F4.y - (this->unk_1E8.y - this->unk_1F4.y);
        sp60.z = this->unk_1F4.z - (this->unk_1E8.z - this->unk_1F4.z);
        u16 buttonsToCheck = BTN_A | BTN_B | BTN_R | BTN_CUP | BTN_CLEFT | BTN_CRIGHT | BTN_CDOWN;
        if (CVarGetInteger(CVAR_ENHANCEMENT("DpadEquips"), 0) != 0) {
            buttonsToCheck |= BTN_DUP | BTN_DDOWN | BTN_DLEFT | BTN_DRIGHT;
        }
        if (BgCheck_EntityLineTest1(&play->colCtx, &sp60, &this->unk_1E8, &sp78, &poly, true, true, true, true,
                                    &bgId) &&
            !func_8002F9EC(play, &this->actor, poly, bgId, &sp78)) {
            sp5C = COLPOLY_GET_NORMAL(poly->normal.x);
            sp58 = COLPOLY_GET_NORMAL(poly->normal.z);
            Math_Vec3f_Copy(&this->actor.world.pos, &sp78);
            //this->actor.world.pos.x += 10.0f * sp5C;
            //this->actor.world.pos.z += 10.0f * sp58;
            this->timer = 0;
            if (SurfaceType_IsHookshotSurface(&play->colCtx, poly, bgId)) {
                if (bgId != BGCHECK_SCENE) {
                    dynaPolyActor = DynaPoly_GetActor(&play->colCtx, bgId);
                    if (dynaPolyActor != NULL) {
                        ArmsHook_AttachHookToActor(this, &dynaPolyActor->actor);
                    }
                }
                func_80865044(this);
                Audio_PlaySoundGeneral(NA_SE_IT_HOOKSHOT_STICK_OBJ, &this->actor.projectedPos, 4,
                                       &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
            } else {
                CollisionCheck_SpawnShieldParticlesMetal(play, &this->actor.world.pos);
                Audio_PlaySoundGeneral(NA_SE_IT_HOOKSHOT_REFLECT, &this->actor.projectedPos, 4,
                                       &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
            }
        } else if (CHECK_BTN_ANY(play->state.input[0].press.button, (buttonsToCheck))) {
            this->timer = 0;
        }
    }
}

void ArmsHook_Update(Actor* thisx, PlayState* play) {
    ArmsHook* this = (ArmsHook*)thisx;

    this->actionFunc(this, play);
    this->unk_1F4 = this->unk_1E8;
}

void ArmsHook_Draw(Actor* thisx, PlayState* play) {
    s32 pad;
    ArmsHook* this = (ArmsHook*)thisx;
    Player* player = GET_PLAYER(play);
    Vec3f sp78;
    Vec3f sp6C;
    Vec3f sp60;
    f32 sp5C;
    f32 sp58;

    if ((player->actor.draw != NULL) && (player->rightHandType == PLAYER_MODELTYPE_RH_HOOKSHOT)) {
        OPEN_DISPS(play->state.gfxCtx);

        if ((ArmsHook_Shoot != this->actionFunc) || (this->timer <= 0)) {
            Matrix_MultVec3f(&D_80865B70, &this->unk_1E8);
            Matrix_MultVec3f(&D_80865B88, &sp6C);
            Matrix_MultVec3f(&D_80865B94, &sp60);
            this->hookInfo.active = 0;
        } else {
            Matrix_MultVec3f(&D_80865B7C, &this->unk_1E8);
            Matrix_MultVec3f(&D_80865BA0, &sp6C);
            Matrix_MultVec3f(&D_80865BAC, &sp60);
        }

        func_80090480(play, &this->collider, &this->hookInfo, &sp6C, &sp60);
        Gfx_SetupDL_25Opa(play->state.gfxCtx);

        // --- DRAW THE HOOK TIP ---
        if (CVarGetInteger(CVAR_ENHANCEMENT("EquipmentAlwaysVisible"), 0) &&
            CVarGetInteger(CVAR_ENHANCEMENT("ScaleAdultEquipmentAsChild"), 0) && LINK_IS_CHILD) {
            Matrix_Scale(0.8, 0.8, 0.8, MTXMODE_APPLY);
        }
        gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        gSPDisplayList(POLY_OPA_DISP++, gLinkAdultHookshotTipDL);

        // --- PREPARE THE CHAIN MATRIX ---
        // 1. Start the matrix at the tip of the hookshot
        Matrix_Translate(this->actor.world.pos.x, this->actor.world.pos.y, this->actor.world.pos.z, MTXMODE_NEW);

        // 2. Calculate vector and distances back to Link's hand
        Math_Vec3f_Diff(&player->unk_3C8, &this->actor.world.pos, &sp78);
        sp58 = SQ(sp78.x) + SQ(sp78.z);
        sp5C = sqrtf(sp58);
        f32 totalDist = sqrtf(SQ(sp78.y) + sp58); // Total distance from tip to hand

        // 3. Rotate the matrix so its Z-axis points directly at Link's hand
        Matrix_RotateY(Math_FAtan2F(sp78.x, sp78.z), MTXMODE_APPLY);
        Matrix_RotateX(Math_FAtan2F(-sp78.y, sp5C), MTXMODE_APPLY);

        // ONLY DRAW THE CHAIN IF WE ARE ACTIVELY SHOOTING OR RETRACTING
        if (this->actionFunc == ArmsHook_Shoot && totalDist > this->speed) {

            // --- SETUP FOR UNTEXTURED 3D GEOMETRY ---
            gDPSetCombineMode(POLY_OPA_DISP++, G_CC_SHADE, G_CC_SHADE);
            gDPSetRenderMode(POLY_OPA_DISP++, G_RM_AA_ZB_OPA_SURF, G_RM_AA_ZB_OPA_SURF2);
            
            f32 zStep = 8.0f; 
            f32 yOffset = -1.5f; // Better centered on the barrel and chain
            f32 startOffsetZ = -3.0f; // Just behind the tip
            int numLinks = (int)(totalDist / zStep) + 1;

            if (numLinks > 150) {
                numLinks = 150; // Crash prevention
            }
            
            for (int i = 0; i < numLinks; i++) {
                Matrix_Push();
                
                f32 zOffset = startOffsetZ + (i * zStep);
                Matrix_Translate(0.0f, yOffset, zOffset, MTXMODE_APPLY);

                if (i % 2 != 0) {
                    Matrix_RotateZ(1.5708f, MTXMODE_APPLY); 
                }

                Matrix_Scale(0.10f, 0.06f, 0.10f, MTXMODE_APPLY);

                gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
                
                // --- DRAW THE 3D CHAIN LINK DYNAMICALLY ---
                // Load all 24 vertices into the RSP cache
                gSPVertex(POLY_OPA_DISP++, sCustom3DChainLinkVtx, 24, 0);
                
                // Front Face
                gSP2Triangles(POLY_OPA_DISP++,  0,  1,  9, 0,  0,  9,  8, 0);
                gSP1Triangle(POLY_OPA_DISP++,   1,  2,  9, 0);
                gSP2Triangles(POLY_OPA_DISP++,  2,  3, 10, 0,  2, 10,  9, 0);
                gSP1Triangle(POLY_OPA_DISP++,   3,  4, 10, 0);
                gSP2Triangles(POLY_OPA_DISP++,  4,  5, 11, 0,  4, 11, 10, 0);
                gSP1Triangle(POLY_OPA_DISP++,   5,  6, 11, 0);
                gSP2Triangles(POLY_OPA_DISP++,  6,  7,  8, 0,  6,  8, 11, 0);
                gSP1Triangle(POLY_OPA_DISP++,   7,  0,  8, 0);
                
                // Back Face
                gSP2Triangles(POLY_OPA_DISP++, 12, 21, 13, 0, 12, 20, 21, 0);
                gSP1Triangle(POLY_OPA_DISP++,  13, 21, 14, 0);
                gSP2Triangles(POLY_OPA_DISP++, 14, 22, 15, 0, 14, 21, 22, 0);
                gSP1Triangle(POLY_OPA_DISP++,  15, 22, 16, 0);
                gSP2Triangles(POLY_OPA_DISP++, 16, 23, 17, 0, 16, 22, 23, 0);
                gSP1Triangle(POLY_OPA_DISP++,  17, 23, 18, 0);
                gSP2Triangles(POLY_OPA_DISP++, 18, 20, 19, 0, 18, 23, 20, 0);
                gSP1Triangle(POLY_OPA_DISP++,  19, 20, 12, 0);
                
                // Outer Walls
                gSP2Triangles(POLY_OPA_DISP++,  1,  0, 12, 0,  1, 12, 13, 0);
                gSP2Triangles(POLY_OPA_DISP++,  2,  1, 13, 0,  2, 13, 14, 0);
                gSP2Triangles(POLY_OPA_DISP++,  3,  2, 14, 0,  3, 14, 15, 0);
                gSP2Triangles(POLY_OPA_DISP++,  4,  3, 15, 0,  4, 15, 16, 0);
                gSP2Triangles(POLY_OPA_DISP++,  5,  4, 16, 0,  5, 16, 17, 0);
                gSP2Triangles(POLY_OPA_DISP++,  6,  5, 17, 0,  6, 17, 18, 0);
                gSP2Triangles(POLY_OPA_DISP++,  7,  6, 18, 0,  7, 18, 19, 0);
                gSP2Triangles(POLY_OPA_DISP++,  0,  7, 19, 0,  0, 19, 12, 0);
                
                // Inner Walls
                gSP2Triangles(POLY_OPA_DISP++,  8,  9, 21, 0,  8, 21, 20, 0);
                gSP2Triangles(POLY_OPA_DISP++,  9, 10, 22, 0,  9, 22, 21, 0);
                gSP2Triangles(POLY_OPA_DISP++, 10, 11, 23, 0, 10, 23, 22, 0);
                gSP2Triangles(POLY_OPA_DISP++, 11,  8, 20, 0, 11, 20, 23, 0);
                
                Matrix_Pop();
            }
            
            // --- CLEANUP PIPELINE STATE ---
            gDPSetCombineMode(POLY_OPA_DISP++, G_CC_MODULATEI_PRIM, G_CC_MODULATEI_PRIM);
        } // End of conditional chain draw

        CLOSE_DISPS(play->state.gfxCtx);
    }
}

// Use this with the custom DL
// // --- DRAW THE CUSTOM SINGLE-LINK CHAIN ---
//         f32 uniformScale = 0.015f;
//         if (CVarGetInteger(CVAR_ENHANCEMENT("EquipmentAlwaysVisible"), 0) &&
//             CVarGetInteger(CVAR_ENHANCEMENT("ScaleAdultEquipmentAsChild"), 0) && LINK_IS_CHILD) {
//             uniformScale = 0.012f;
//         }

//         // Define the exact length of your custom single link model (scaled).
//         // You will need to tweak this value to match your specific custom model's length
//         // so that they touch end-to-end perfectly.
//         f32 linkLength = 10.0f;

//         // Calculate the number of links needed.
//         // The +1 ensures the chain always fully reaches the hand.
//         // The slight excess will harmlessly clip inside Link's wrist/gun.
//         int numLinks = (int)(totalDist / linkLength) + 1;

//         for (int i = 0; i < numLinks; i++) {
//             Matrix_Push();

//             // Push each link end-to-end down the Z-axis
//             f32 zOffset = i * linkLength;
//             Matrix_Translate(0.0f, 0.0f, zOffset, MTXMODE_APPLY);

//             // Rotate every other link 90 degrees (1.5708 radians) for a realistic interlocking chain look
//             if (i % 2 != 0) {
//                 Matrix_RotateZ(1.5708f, MTXMODE_APPLY);
//             }

//             // Apply uniform scale in all directions (no stretching/squashing!)
//             Matrix_Scale(uniformScale, uniformScale, uniformScale, MTXMODE_APPLY);

//             gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD |
//             G_MTX_MODELVIEW);

//             // Replace this with your custom display list variable
//             gSPDisplayList(POLY_OPA_DISP++, gCustomSingleLinkDL);

//             Matrix_Pop();
//         }

//         CLOSE_DISPS(play->state.gfxCtx);
//     }
// }

// void ArmsHook_Draw(Actor* thisx, PlayState* play) {
//     s32 pad;
//     ArmsHook* this = (ArmsHook*)thisx;
//     Player* player = GET_PLAYER(play);
//     Vec3f sp78;
//     Vec3f sp6C;
//     Vec3f sp60;
//     f32 sp5C;
//     f32 sp58;

//     if ((player->actor.draw != NULL) && (player->rightHandType == PLAYER_MODELTYPE_RH_HOOKSHOT)) {
//         OPEN_DISPS(play->state.gfxCtx);

//         if ((ArmsHook_Shoot != this->actionFunc) || (this->timer <= 0)) {
//             Matrix_MultVec3f(&D_80865B70, &this->unk_1E8);
//             Matrix_MultVec3f(&D_80865B88, &sp6C);
//             Matrix_MultVec3f(&D_80865B94, &sp60);
//             this->hookInfo.active = 0;
//         } else {
//             Matrix_MultVec3f(&D_80865B7C, &this->unk_1E8);
//             Matrix_MultVec3f(&D_80865BA0, &sp6C);
//             Matrix_MultVec3f(&D_80865BAC, &sp60);
//         }

//         func_80090480(play, &this->collider, &this->hookInfo, &sp6C, &sp60);
//         Gfx_SetupDL_25Opa(play->state.gfxCtx);
//         if (CVarGetInteger(CVAR_ENHANCEMENT("EquipmentAlwaysVisible"), 0) &&
//             CVarGetInteger(CVAR_ENHANCEMENT("ScaleAdultEquipmentAsChild"), 0) && LINK_IS_CHILD) {
//             Matrix_Scale(0.8, 0.8, 0.8, MTXMODE_APPLY);
//         }
//         gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
//         gSPDisplayList(POLY_OPA_DISP++, gLinkAdultHookshotTipDL);
//         Matrix_Translate(this->actor.world.pos.x, this->actor.world.pos.y, this->actor.world.pos.z, MTXMODE_NEW);
//         Math_Vec3f_Diff(&player->unk_3C8, &this->actor.world.pos, &sp78);
//         sp58 = SQ(sp78.x) + SQ(sp78.z);
//         sp5C = sqrtf(sp58);
//         Matrix_RotateY(Math_FAtan2F(sp78.x, sp78.z), MTXMODE_APPLY);
//         Matrix_RotateX(Math_FAtan2F(-sp78.y, sp5C), MTXMODE_APPLY);
//         if (CVarGetInteger(CVAR_ENHANCEMENT("EquipmentAlwaysVisible"), 0) &&
//             CVarGetInteger(CVAR_ENHANCEMENT("ScaleAdultEquipmentAsChild"), 0) && LINK_IS_CHILD) {
//             Matrix_Scale(0.012f, 0.012f, sqrtf(SQ(sp78.y) + sp58) * 0.01f, MTXMODE_APPLY);
//         } else {
//             Matrix_Scale(0.015f, 0.015f, sqrtf(SQ(sp78.y) + sp58) * 0.01f, MTXMODE_APPLY);
//         }
//         gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
//         gSPDisplayList(POLY_OPA_DISP++, gLinkAdultHookshotChainDL);

//         CLOSE_DISPS(play->state.gfxCtx);
//     }
// }
