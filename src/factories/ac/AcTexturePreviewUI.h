#pragma once

#include "factories/BaseFactory.h"

#ifdef BUILD_UI
namespace AC {

class TexturePreviewUI : public BaseFactoryUI {
  public:
    float GetItemHeight(const ParseResultData& item) override;
    void DrawUI(const ParseResultData& item) override;
};

} // namespace AC
#endif
