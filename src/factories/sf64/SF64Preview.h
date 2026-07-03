#pragma once

#include "factories/BaseFactory.h"

// SF64 viewer cards: a 3D skeleton preview (limb hierarchy + display lists) and
// a decoded-text card for messages. Other SF64 data/script types use the
// default code-preview card (their factories opt in via CanPreviewCode).
#ifdef BUILD_UI
namespace SF64 {

class SkeletonFactoryUI : public BaseFactoryUI {
public:
    float GetItemHeight(const ParseResultData& data) override;
    void DrawUI(const ParseResultData& data) override;
};

class MessageFactoryUI : public BaseFactoryUI {
public:
    float GetItemHeight(const ParseResultData& data) override;
    void DrawUI(const ParseResultData& data) override;
};

// 3D collision-mesh preview (indexed triangles + a resolved vertex array).
class ColPolyFactoryUI : public BaseFactoryUI {
public:
    float GetItemHeight(const ParseResultData& data) override;
    void DrawUI(const ParseResultData& data) override;
};

class TriangleFactoryUI : public BaseFactoryUI {
public:
    float GetItemHeight(const ParseResultData& data) override;
    void DrawUI(const ParseResultData& data) override;
};

// 3D hitbox-volume preview (axis-aligned boxes from the hitbox float array).
class HitboxFactoryUI : public BaseFactoryUI {
public:
    float GetItemHeight(const ParseResultData& data) override;
    void DrawUI(const ParseResultData& data) override;
};

} // namespace SF64
#endif
