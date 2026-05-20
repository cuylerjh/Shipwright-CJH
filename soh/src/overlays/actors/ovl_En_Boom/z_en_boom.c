/*
 * File: z_en_boom.c
 * Overlay: ovl_En_Boom
 * Description: Thrown Boomerang. Actor spawns when thrown and is killed when caught.
 */

#include "z_en_boom.h"
#include "objects/gameplay_keep/gameplay_keep.h"

extern void Player_DrawBoomerangTargetArrow(PlayState* play, Actor* actor);

#define FLAGS (ACTOR_FLAG_UPDATE_CULLING_DISABLED | ACTOR_FLAG_DRAW_CULLING_DISABLED)

void EnBoom_Init(Actor* thisx, PlayState* play);
void EnBoom_Destroy(Actor* thisx, PlayState* play);
void EnBoom_Update(Actor* thisx, PlayState* play);
void EnBoom_Draw(Actor* thisx, PlayState* play);

void EnBoom_Fly(EnBoom* this, PlayState* play);

const ActorInit En_Boom_InitVars = {
    ACTOR_EN_BOOM,
    ACTORCAT_MISC,
    FLAGS,
    OBJECT_GAMEPLAY_KEEP,
    sizeof(EnBoom),
    (ActorFunc)EnBoom_Init,
    (ActorFunc)EnBoom_Destroy,
    (ActorFunc)EnBoom_Update,
    (ActorFunc)EnBoom_Draw,
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
        { 0x00000010, 0x00, 0x01 },
        { 0xFFCFFFFF, 0x00, 0x00 },
        TOUCH_ON | TOUCH_NEAREST | TOUCH_SFX_NORMAL,
        BUMP_NONE,
        OCELEM_NONE,
    },
    { { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } } },
};

static InitChainEntry sInitChain[] = {
    ICHAIN_S8(targetMode, 5, ICHAIN_CONTINUE),
    ICHAIN_VEC3S(shape.rot, 0, ICHAIN_STOP),
};

void EnBoom_SetupAction(EnBoom* this, EnBoomActionFunc actionFunc) {
    this->actionFunc = actionFunc;
}

void EnBoom_Init(Actor* thisx, PlayState* play) {
    EnBoom* this = (EnBoom*)thisx;
    EffectBlureInit1 blure;

    this->actor.room = -1;
    this->speed = 20.0f;

    Actor_ProcessInitChain(&this->actor, sInitChain);

    blure.p1StartColor[0] = 255;
    blure.p1StartColor[1] = 255;
    blure.p1StartColor[2] = 100;
    blure.p1StartColor[3] = 255;

    blure.p2StartColor[0] = 255;
    blure.p2StartColor[1] = 255;
    blure.p2StartColor[2] = 100;
    blure.p2StartColor[3] = 64;

    blure.p1EndColor[0] = 255;
    blure.p1EndColor[1] = 255;
    blure.p1EndColor[2] = 100;
    blure.p1EndColor[3] = 0;

    blure.p2EndColor[0] = 255;
    blure.p2EndColor[1] = 255;
    blure.p2EndColor[2] = 100;
    blure.p2EndColor[3] = 0;

    blure.elemDuration = 8;
    blure.unkFlag = 0;
    blure.calcMode = 0;
    blure.trailType = TRAIL_TYPE_BOOMERANG;

    Effect_Add(play, &this->effectIndex1, EFFECT_BLURE1, 0, 0, &blure);
    Effect_Add(play, &this->effectIndex2, EFFECT_BLURE1, 0, 0, &blure);

    this->targetCount = 0; // Will be overwritten by the player in a millisecond
    this->currentTargetIndex = 0;

    Player* player = GET_PLAYER(play);
    s16 pYaw = player->actor.shape.rot.y;
    Vec3f* pPos = &player->actor.world.pos;

    Collider_InitQuad(play, &this->collider);
    Collider_SetQuad(play, &this->collider, &this->actor, &sQuadInit);

    EnBoom_SetupAction(this, EnBoom_Fly);
}

void EnBoom_Destroy(Actor* thisx, PlayState* play) {
    EnBoom* this = (EnBoom*)thisx;

    Effect_Delete(play, this->effectIndex1);
    Effect_Delete(play, this->effectIndex2);
    Collider_DestroyQuad(play, &this->collider);
}

void EnBoom_Caught(EnBoom* this, PlayState* play) {
    if (this->returnTimer == 0) {
        Actor_Kill(&this->actor);
    } else {
        this->returnTimer--;
    }
}

void EnBoom_Fly(EnBoom* this, PlayState* play) {
    Actor* target;
    Player* player = GET_PLAYER(play);
    s32 collided;
    s16 yawTarget;
    s16 yawDiff;
    s16 pitchTarget;
    s16 pitchDiff;
    f32 distXYZScale;
    f32 distFromLink;
    DynaPolyActor* hitActor;
    s32 hitDynaID;
    Vec3f hitPoint;
    Vec3f targetPos;
    Actor* currentTarget = NULL;
    u8 isReturning = false;

    // --- 1. DETERMINE CURRENT DESTINATION & STATE ---
    while (this->currentTargetIndex < this->targetCount) {
        currentTarget = this->targetActors[this->currentTargetIndex];

        // Failsafe: If the actor was destroyed or deactivated before we reached it, skip it
        if (currentTarget == NULL || currentTarget->update == NULL) {
            this->currentTargetIndex++;
        } else {
            // Target is valid and living! Set the destination and break the loop.
            // We NO LONGER check the distance here. The boomerang MUST physically 
            // strike the target (triggering AT_HIT below) to advance the index!
            targetPos = currentTarget->focus.pos;
            break; 
        }
    }

    if (this->returnTimer == 0 || player->boomerangQuickRecall || (this->targetCount > 0 && this->currentTargetIndex >= this->targetCount)) {
        isReturning = true;
        this->returnTimer = 0; // Lock the timer at 0 to ensure it stays returning
    }

    // --- 2. STEER TOWARD DESTINATION ---
    if (isReturning || (this->currentTargetIndex < this->targetCount && currentTarget != NULL)) {
        
        if (isReturning) {
            targetPos = player->actor.focus.pos; 
        } else {
            targetPos = currentTarget->focus.pos; 
        }

        yawTarget = Actor_WorldYawTowardPoint(&this->actor, &targetPos);
        yawDiff = this->actor.world.rot.y - yawTarget;

        pitchTarget = Actor_WorldPitchTowardPoint(&this->actor, &targetPos);
        pitchDiff = this->actor.world.rot.x - pitchTarget;

        distXYZScale = (200.0f - Math_Vec3f_DistXYZ(&this->actor.world.pos, &targetPos)) * 0.005f;
        if (distXYZScale < 0.12f) {
            distXYZScale = 0.12f;
        }
        
        // --- TURN RADIUS BOOST ---
        // Increase the turning responsiveness by 15% to prevent undershooting
        distXYZScale *= 1.15f; 

        Math_ScaledStepToS(&this->actor.world.rot.y, yawTarget, (s16)(ABS(yawDiff) * distXYZScale));
        Math_ScaledStepToS(&this->actor.world.rot.x, pitchTarget, (s16)(ABS(pitchDiff) * distXYZScale));
    }
    
    // Set xyz speed, move forward, and play sound
    Actor_SetProjectileSpeed(&this->actor, this->speed);
    Actor_MoveXZGravity(&this->actor);
    func_8002F974(&this->actor, NA_SE_IT_BOOMERANG_FLY - SFX_FLAG);

    // --- 3. GRAB AND COLLISION LOGIC ---
    collided = !!(this->collider.base.atFlags & AT_HIT);
    if (collided) {
        if ((this->collider.base.at->id == ACTOR_EN_ITEM00) || (this->collider.base.at->id == ACTOR_EN_SI)) {
            this->grabbed = this->collider.base.at;
            if (this->collider.base.at->id == ACTOR_EN_SI) {
                this->collider.base.at->flags |= ACTOR_FLAG_HOOKSHOT_ATTACHED;
            }
        }
    }

    // --- 4. RETURN AND CATCH LOGIC ---
    if (DECR(this->returnTimer) == 0 || player->boomerangQuickRecall) {
        distFromLink = Math_Vec3f_DistXYZ(&this->actor.world.pos, &player->actor.focus.pos);

        this->currentTargetIndex = this->targetCount;
        this->moveTo = &player->actor;

        if (distFromLink <= this->speed || player->boomerangQuickRecall) {
            target = this->grabbed;
            if (target != NULL) {
                Math_Vec3f_Copy(&target->world.pos, &player->actor.world.pos);

                if (target->id == ACTOR_EN_ITEM00) {
                    target->gravity = -0.9f;
                    target->bgCheckFlags &= ~0x03;
                } else {
                    target->flags &= ~ACTOR_FLAG_HOOKSHOT_ATTACHED;
                }
            }
            player->stateFlags1 &= ~PLAYER_STATE1_BOOMERANG_THROWN;
            player->boomerangQuickRecall = false;
            this->actor.draw = NULL;
            this->returnTimer = 8;
            EnBoom_SetupAction(this, EnBoom_Caught);
        }
    } else {
        // Check our collision types
        s32 atHit = !!(this->collider.base.atFlags & AT_HIT);
        s32 bouncedOffHard = !!(this->collider.base.atFlags & AT_BOUNCED); // Check for AC_HARD
        s32 hitWall = 0;

        if (atHit) {
            if (bouncedOffHard) {
                hitWall = true; 
            } else {
                // We hit an enemy! If it's our current target, move on!
                if (this->currentTargetIndex < this->targetCount && this->collider.base.at == currentTarget) {
                    this->currentTargetIndex++;
                    
                    if (this->currentTargetIndex >= this->targetCount) {
                        this->returnTimer = 0; // Trigger return phase
                    } else {
                        // --- WIND WAKER DYNAMIC TIMER ---
                        Actor* nextTarget = this->targetActors[this->currentTargetIndex];
                        if (nextTarget != NULL && nextTarget->update != NULL) { 
                            f32 distToNext = Math_Vec3f_DistXYZ(&this->actor.world.pos, &nextTarget->focus.pos);
                            // Time = Distance / Speed. Add 15 frames for curve buffer!
                            this->returnTimer = (u8)(distToNext / this->speed) + 15; 
                        } else {
                            this->returnTimer = 30; // Fallback
                        }
                    }
                }
            }
        } 
        
        // Background wall check
        if (!hitWall && !atHit) {
            hitWall = BgCheck_EntityLineTest1(&play->colCtx, &this->actor.prevPos, &this->actor.world.pos, &hitPoint,
                                               &this->actor.wallPoly, true, true, true, true, &hitDynaID);

            if (hitWall) {
                if (func_8002F9EC(play, &this->actor, this->actor.wallPoly, hitDynaID, &hitPoint) != 0 ||
                    (hitDynaID != BGCHECK_SCENE && ((hitActor = DynaPoly_GetActor(&play->colCtx, hitDynaID)) != NULL) &&
                     hitActor->actor.id == ACTOR_BG_BDAN_OBJECTS && hitActor->actor.params == 0)) {
                    hitWall = false;
                } else {
                    CollisionCheck_SpawnShieldParticlesMetal(play, &hitPoint);
                }
            }
        }

        // Restored wall/hard bounce logic
        if (hitWall) {
            this->actor.world.rot.x = -this->actor.world.rot.x;
            this->actor.world.rot.y += 0x8000;
            this->currentTargetIndex = this->targetCount; 
            this->moveTo = &player->actor;
            this->returnTimer = 0;
        }
    }

    target = this->grabbed;
    if (target != NULL) {
        if (target->update == NULL) {
            this->grabbed = NULL;
        } else {
            Math_Vec3f_Copy(&target->world.pos, &this->actor.world.pos);
        }
    }
}

void EnBoom_Update(Actor* thisx, PlayState* play) {
    EnBoom* this = (EnBoom*)thisx;
    Player* player = GET_PLAYER(play);

    if (!(player->stateFlags1 & PLAYER_STATE1_IN_CUTSCENE)) {
        this->actionFunc(this, play);
        Actor_SetFocus(&this->actor, 0.0f);
        this->activeTimer++;
    }
}

void EnBoom_Draw(Actor* thisx, PlayState* play) {
    // Vectors for the physical damage hitbox (full width)
    static Vec3f sHitbox_v1 = { -960.0f, 0.0f, 0.0f };
    static Vec3f sHitbox_v2 = { 960.0f, 0.0f, 0.0f };

    // Vectors for Trail 1 (Inner set to -400, outer set to 800)
    static Vec3f sTrail1_Inner = { -400.0f, 0.0f, 0.0f };
    static Vec3f sTrail1_Outer = { 800.0f, 0.0f, 0.0f };

    // Vectors for Trail 2 (Inner set to 400, outer set to -800)
    static Vec3f sTrail2_Inner = { 400.0f, 0.0f, 0.0f };
    static Vec3f sTrail2_Outer = { -800.0f, 0.0f, 0.0f };

    EnBoom* this = (EnBoom*)thisx;
    Vec3f hitbox_v1, hitbox_v2;
    Vec3f trail1_v1, trail1_v2;
    Vec3f trail2_v1, trail2_v2;

    OPEN_DISPS(play->state.gfxCtx);

    Matrix_RotateY(this->actor.world.rot.y * (M_PI / 0x8000), MTXMODE_APPLY);
    Matrix_RotateZ(0x1F40 * (M_PI / 0x8000), MTXMODE_APPLY);
    Matrix_RotateX(this->actor.world.rot.x * (M_PI / 0x8000), MTXMODE_APPLY);

    // Spin the boomerang
    Matrix_RotateY((this->activeTimer * 8000) * (M_PI / 0x8000), MTXMODE_APPLY);

    // 1. Calculate the full-width vectors for the hitbox
    Matrix_MultVec3f(&sHitbox_v1, &hitbox_v1);
    Matrix_MultVec3f(&sHitbox_v2, &hitbox_v2);

    // 2. Calculate the smaller vectors for the two distinct visual trails
    Matrix_MultVec3f(&sTrail1_Inner, &trail1_v1);
    Matrix_MultVec3f(&sTrail1_Outer, &trail1_v2);
    Matrix_MultVec3f(&sTrail2_Inner, &trail2_v1);
    Matrix_MultVec3f(&sTrail2_Outer, &trail2_v2);

    // Pass the full-size HITBOX vectors to the collision checker
    if (func_80090480(play, &this->collider, &this->boomerangInfo, &hitbox_v1, &hitbox_v2) != 0) {
        // Swap v2 (outer) and v1 (inner) so the bright p1StartColor goes to the outer edge
        EffectBlure_AddVertex(Effect_GetByIndex(this->effectIndex1), &trail1_v2, &trail1_v1);
        EffectBlure_AddVertex(Effect_GetByIndex(this->effectIndex2), &trail2_v2, &trail2_v1);
    }

    Gfx_SetupDL_25Opa(play->state.gfxCtx);

    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gSPDisplayList(POLY_OPA_DISP++, gBoomerangRefDL);

    CLOSE_DISPS(play->state.gfxCtx);

    // --- WIND WAKER IN-FLIGHT TARGET ARROWS ---
    // Loop through the targets starting at the CURRENT index.
    // When the boomerang hits a target and increments the index, the arrow is naturally skipped!
    for (int i = this->currentTargetIndex; i < this->targetCount; i++) {
        Actor* lockedActor = this->targetActors[i];
        if (lockedActor != NULL && lockedActor->update != NULL) {
            Player_DrawBoomerangTargetArrow(play, lockedActor);
        }
    }
}