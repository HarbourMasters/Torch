#pragma once

#ifdef BUILD_UI

#include "ui/audio/SequenceDriver.h"

// naudio v0 (SM64-era library) m64 sequence driver.
class SequencePlayerV0 : public UI::SequenceDriver {
public:
    bool Render(const ParseResultData& item, int option, UI::RenderedAudio& out) override;
};

#endif // BUILD_UI
