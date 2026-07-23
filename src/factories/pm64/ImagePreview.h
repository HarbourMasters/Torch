#pragma once

#include "factories/BaseFactory.h"

// Viewer cards for the PM64 image-like assets: sprites, map texture archives,
// imgfx vertex animations, title screen images and intro story images.
#ifdef BUILD_UI
class PM64SpriteFactoryUI : public BaseFactoryUI {
public:
    float GetItemHeight(const ParseResultData& data) override;
    void DrawUI(const ParseResultData& data) override;
};

class PM64MapTextureFactoryUI : public BaseFactoryUI {
public:
    float GetItemHeight(const ParseResultData& data) override;
    void DrawUI(const ParseResultData& data) override;
};

class PM64ImgFXAnimFactoryUI : public BaseFactoryUI {
public:
    float GetItemHeight(const ParseResultData& data) override;
    void DrawUI(const ParseResultData& data) override;
};

class PM64TitleDataFactoryUI : public BaseFactoryUI {
public:
    float GetItemHeight(const ParseResultData& data) override;
    void DrawUI(const ParseResultData& data) override;
};

class PM64StoryImageFactoryUI : public BaseFactoryUI {
public:
    float GetItemHeight(const ParseResultData& data) override;
    void DrawUI(const ParseResultData& data) override;
};
#endif
