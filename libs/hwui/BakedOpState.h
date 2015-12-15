/*
 * Copyright (C) 2015 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef ANDROID_HWUI_BAKED_OP_STATE_H
#define ANDROID_HWUI_BAKED_OP_STATE_H

#include "Matrix.h"
#include "RecordedOp.h"
#include "Rect.h"
#include "Snapshot.h"

namespace android {
namespace uirenderer {

namespace OpClipSideFlags {
    enum {
        None = 0x0,
        Left = 0x1,
        Top = 0x2,
        Right = 0x4,
        Bottom = 0x8,
        Full = 0xF,
        // ConservativeFull = 0x1F  needed?
    };
}

/**
 * Holds a list of BakedOpStates of ops that can be drawn together
 */
struct MergedBakedOpList {
    const BakedOpState*const* states;
    size_t count;
    int clipSideFlags;
    Rect clip;
};

/**
 * Holds the resolved clip, transform, and bounds of a recordedOp, when replayed with a snapshot
 */
class ResolvedRenderState {
public:
    // TODO: remove the mapRects/matrix multiply when snapshot & recorded transforms are translates
    ResolvedRenderState(const Snapshot& snapshot, const RecordedOp& recordedOp, bool expandForStroke) {
        /* TODO: benchmark a fast path for translate-only matrices, such as:
        if (CC_LIKELY(snapshot.transform->getType() == Matrix4::kTypeTranslate
                && recordedOp.localMatrix.getType() == Matrix4::kTypeTranslate)) {
            float translateX = snapshot.transform->getTranslateX() + recordedOp.localMatrix.getTranslateX();
            float translateY = snapshot.transform->getTranslateY() + recordedOp.localMatrix.getTranslateY();
            transform.loadTranslate(translateX, translateY, 0);

            // resolvedClipRect = intersect(parentMatrix * localClip, parentClip)
            clipRect = recordedOp.localClipRect;
            clipRect.translate(translateX, translateY);
            clipRect.doIntersect(snapshot.getClipRect());
            clipRect.snapToPixelBoundaries();

            // resolvedClippedBounds = intersect(resolvedMatrix * opBounds, resolvedClipRect)
            clippedBounds = recordedOp.unmappedBounds;
            clippedBounds.translate(translateX, translateY);
        } ... */

        // resolvedMatrix = parentMatrix * localMatrix
        transform.loadMultiply(*snapshot.transform, recordedOp.localMatrix);

        // resolvedClipRect = intersect(parentMatrix * localClip, parentClip)
        clipRect = recordedOp.localClipRect;
        snapshot.transform->mapRect(clipRect);
        clipRect.doIntersect(snapshot.getRenderTargetClip());
        clipRect.snapToPixelBoundaries();

        // resolvedClippedBounds = intersect(resolvedMatrix * opBounds, resolvedClipRect)
        clippedBounds = recordedOp.unmappedBounds;
        if (CC_UNLIKELY(expandForStroke)) {
            // account for non-hairline stroke
            clippedBounds.outset(recordedOp.paint->getStrokeWidth() * 0.5f);
        }
        transform.mapRect(clippedBounds);
        if (CC_UNLIKELY(expandForStroke
                && (!transform.isPureTranslate() || recordedOp.paint->getStrokeWidth() < 1.0f))) {
            // account for hairline stroke when stroke may be < 1 scaled pixel
            // Non translate || strokeWidth < 1 is conservative, but will cover all cases
            clippedBounds.outset(0.5f);
        }

        if (clipRect.left > clippedBounds.left) clipSideFlags |= OpClipSideFlags::Left;
        if (clipRect.top > clippedBounds.top) clipSideFlags |= OpClipSideFlags::Top;
        if (clipRect.right < clippedBounds.right) clipSideFlags |= OpClipSideFlags::Right;
        if (clipRect.bottom < clippedBounds.bottom) clipSideFlags |= OpClipSideFlags::Bottom;
        clippedBounds.doIntersect(clipRect);

        /**
         * TODO: once we support complex clips, we may want to reject to avoid that work where
         * possible. Should we:
         * 1 - quickreject based on clippedBounds, quick early (duplicating logic in resolvedOp)
         * 2 - merge stuff into tryConstruct factory method, so it can handle quickRejection
         *         and early return null in one place.
         */
    }

    /**
     * Constructor for unbounded ops without transform/clip (namely shadows)
     *
     * Since the op doesn't have known bounds, we conservatively set the mapped bounds
     * to the current clipRect, and clipSideFlags to Full.
     */
    ResolvedRenderState(const Snapshot& snapshot) {
        transform = *snapshot.transform;
        clipRect = snapshot.getRenderTargetClip();
        clippedBounds = clipRect;
        transform.mapRect(clippedBounds);
        clipSideFlags = OpClipSideFlags::Full;
    }

    Rect computeLocalSpaceClip() const {
        Matrix4 inverse;
        inverse.loadInverse(transform);

        Rect outClip(clipRect);
        inverse.mapRect(outClip);
        return outClip;
    }

    Matrix4 transform;
    Rect clipRect;
    int clipSideFlags = 0;
    Rect clippedBounds;
};

/**
 * Self-contained op wrapper, containing all resolved state required to draw the op.
 *
 * Stashed pointers within all point to longer lived objects, with no ownership implied.
 */
class BakedOpState {
public:
    static BakedOpState* tryConstruct(LinearAllocator& allocator,
            const Snapshot& snapshot, const RecordedOp& recordedOp) {
        BakedOpState* bakedState = new (allocator) BakedOpState(snapshot, recordedOp, false);
        if (bakedState->computedState.clippedBounds.isEmpty()) {
            // bounds are empty, so op is rejected
            allocator.rewindIfLastAlloc(bakedState);
            return nullptr;
        }
        return bakedState;
    }

    enum class StrokeBehavior {
        // stroking is forced, regardless of style on paint
        Forced,
        // stroking is defined by style on paint
        StyleDefined,
    };

    static BakedOpState* tryStrokeableOpConstruct(LinearAllocator& allocator,
            const Snapshot& snapshot, const RecordedOp& recordedOp, StrokeBehavior strokeBehavior) {
        bool expandForStroke = (strokeBehavior == StrokeBehavior::StyleDefined)
                ? (recordedOp.paint && recordedOp.paint->getStyle() != SkPaint::kFill_Style)
                : true;

        BakedOpState* bakedState = new (allocator) BakedOpState(
                snapshot, recordedOp, expandForStroke);
        if (bakedState->computedState.clippedBounds.isEmpty()) {
            // bounds are empty, so op is rejected
            allocator.rewindIfLastAlloc(bakedState);
            return nullptr;
        }
        return bakedState;
    }

    static BakedOpState* tryShadowOpConstruct(LinearAllocator& allocator,
            const Snapshot& snapshot, const ShadowOp* shadowOpPtr) {
        if (snapshot.getRenderTargetClip().isEmpty()) return nullptr;

        // clip isn't empty, so construct the op
        return new (allocator) BakedOpState(snapshot, shadowOpPtr);
    }

    static void* operator new(size_t size, LinearAllocator& allocator) {
        return allocator.alloc(size);
    }

    // computed state:
    const ResolvedRenderState computedState;

    // simple state (straight pointer/value storage):
    const float alpha;
    const RoundRectClipState* roundRectClipState;
    const ProjectionPathMask* projectionPathMask;
    const RecordedOp* op;

private:
    BakedOpState(const Snapshot& snapshot, const RecordedOp& recordedOp, bool expandForStroke)
            : computedState(snapshot, recordedOp, expandForStroke)
            , alpha(snapshot.alpha)
            , roundRectClipState(snapshot.roundRectClipState)
            , projectionPathMask(snapshot.projectionPathMask)
            , op(&recordedOp) {}

    BakedOpState(const Snapshot& snapshot, const ShadowOp* shadowOpPtr)
            : computedState(snapshot)
            , alpha(snapshot.alpha)
            , roundRectClipState(snapshot.roundRectClipState)
            , projectionPathMask(snapshot.projectionPathMask)
            , op(shadowOpPtr) {}
};

}; // namespace uirenderer
}; // namespace android

#endif // ANDROID_HWUI_BAKED_OP_STATE_H
