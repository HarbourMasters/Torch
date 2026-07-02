#pragma once

#ifdef BUILD_UI

#include "ui/audio/SequenceDriver.h"

// naudio v1 (SF64-era library) sequence driver. Fonts resolve through the
// game's seq-font table (extracted as the gSeqFontTable ARRAY asset).
class SequencePlayerV1 : public UI::SequenceDriver {
public:
    bool Render(const ParseResultData& item, int option, UI::RenderedAudio& out) override;
};

#endif // BUILD_UI
