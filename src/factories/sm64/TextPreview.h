#pragma once

#include "factories/BaseFactory.h"

// Decoded-text viewer cards for the SM64 string assets (dialog, course/menu
// text, and the compression dictionary). These decode the game's custom
// character encoding into readable UTF-8.
#ifdef BUILD_UI
namespace SM64 {

class DialogFactoryUI : public BaseFactoryUI {
public:
    float GetItemHeight(const ParseResultData& data) override;
    void DrawUI(const ParseResultData& data) override;
};

class TextFactoryUI : public BaseFactoryUI {
public:
    float GetItemHeight(const ParseResultData& data) override;
    void DrawUI(const ParseResultData& data) override;
};

class DictionaryFactoryUI : public BaseFactoryUI {
public:
    float GetItemHeight(const ParseResultData& data) override;
    void DrawUI(const ParseResultData& data) override;
};

} // namespace SM64
#endif
