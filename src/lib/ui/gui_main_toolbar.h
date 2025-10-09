/*******************************************************************************************
*
*   Main Toolbar
*
*   MODULE USAGE:
*       #define GUI_MAIN_TOOLBAR_IMPLEMENTATION
*       #include "gui_main_toolbar.h"
*
*       INIT: GuiMainToolbarState state = InitGuiMainToolbar();
*       DRAW: GuiMainToolbar(&state);
*
*   LICENSE: zlib/libpng
*
*   Copyright (c) 2019-2022 raylib technologies (@raylibtech) / Ramon Santamaria (@raysan5)
*
*   This software is provided "as-is", without any express or implied warranty. In no event
*   will the authors be held liable for any damages arising from the use of this software.
*
*   Permission is granted to anyone to use this software for any purpose, including commercial
*   applications, and to alter it and redistribute it freely, subject to the following restrictions:
*
*     1. The origin of this software must not be misrepresented; you must not claim that you
*     wrote the original software. If you use this software in a product, an acknowledgment
*     in the product documentation would be appreciated but is not required.
*
*     2. Altered source versions must be plainly marked as such, and must not be misrepresented
*     as being the original software.
*
*     3. This notice may not be removed or altered from any source distribution.
*
**********************************************************************************************/

#include "raylib.h"

// WARNING: raygui implementation is expected to be defined before including this header

#ifndef GUI_MAIN_TOOLBAR_H
#define GUI_MAIN_TOOLBAR_H

typedef struct {

    // Anchors for panels
    Vector2 anchorFile;
    Vector2 anchorEdit;
    Vector2 anchorTools;
    Vector2 anchorVisuals;
    Vector2 anchorRight;
    
    // File options
    bool btnNewFilePressed;
    bool btnLoadFilePressed;
    bool btnSaveFilePressed;
    bool btnExportFilePressed;

    // Editor options
    bool btnCutPressed;
    bool btnCopyPressed;
    bool btnPastePressed;
    bool btnCleanPressed;

    // Tool options
    //...

    // Visual options
    int visualStyleActive;
    int prevVisualStyleActive;
    int btnReloadStylePressed;
    int languageActive;

    // Info options
    bool btnHelpPressed;
    bool btnAboutPressed;
    bool btnIssuePressed;
    bool btnUserPressed;

    // Custom variables
    // TODO.

} GuiMainToolbarState;

#ifdef __cplusplus
extern "C" {            // Prevents name mangling of functions
#endif

//----------------------------------------------------------------------------------
// Defines and Macros
//----------------------------------------------------------------------------------
//...

//----------------------------------------------------------------------------------
// Types and Structures Definition
//----------------------------------------------------------------------------------
// ...

//----------------------------------------------------------------------------------
// Global Variables Definition
//----------------------------------------------------------------------------------
//...

//----------------------------------------------------------------------------------
// Internal Module Functions Definition
//----------------------------------------------------------------------------------
//...

//----------------------------------------------------------------------------------
// Module Functions Declaration
//----------------------------------------------------------------------------------
GuiMainToolbarState InitGuiMainToolbar(void);
void GuiMainToolbar(GuiMainToolbarState *state);

#ifdef __cplusplus
}
#endif

#endif // GUI_MAIN_TOOLBAR_H

/***********************************************************************************
*
*   GUI_MAIN_TOOLBAR IMPLEMENTATION
*
************************************************************************************/
#if defined(GUI_MAIN_TOOLBAR_IMPLEMENTATION)

#include "raygui.h"

GuiMainToolbarState InitGuiMainToolbar(void)
{
    GuiMainToolbarState state = { 0 };

    // Anchors for panels
    state.anchorFile = (Vector2){ 0, 0 };
    state.anchorEdit = (Vector2){ state.anchorFile.x + 132 - 1, 0 };
    state.anchorTools = (Vector2){ state.anchorEdit.x + 168 - 1, 0 };
    state.anchorVisuals = (Vector2){ 0, 0 };    // Anchor right, depends on screen width
    state.anchorRight = (Vector2){ 0, 0 };      // Anchor right, depends on screen width

    // Project/File options
    state.btnNewFilePressed = false;
    state.btnLoadFilePressed = false;
    state.btnSaveFilePressed = false;
    state.btnExportFilePressed = false;

    // Edit options
    state.btnCutPressed = false;
    state.btnCopyPressed = false;
    state.btnPastePressed = false;
    state.btnCleanPressed = false;

    // Tool options
    //...

    // Visuals options
    state.visualStyleActive = 0;
    state.prevVisualStyleActive = 0;
    state.languageActive = 0;

    // Info options
    state.btnHelpPressed = false;
    state.btnAboutPressed = false;
    state.btnIssuePressed = false;
    state.btnUserPressed = false;
    
    // Custom variables
    // TODO.

    // Enable tooltips by default
    GuiEnableTooltip();

    return state;
}

void GuiMainToolbar(GuiMainToolbarState *state)
{
    int screenWidth = 640;  // WARNING: Screen width is hardcoded to avoid issues on screen scaling!

    // Toolbar panels
    state->anchorRight.x = screenWidth - 104;       // Update right-anchor panel
    state->anchorVisuals.x = state->anchorRight.x - 190 + 1;    // Update right-anchor panel

    GuiPanel((Rectangle){ state->anchorFile.x, state->anchorFile.y, 132, 40 }, NULL);
    GuiPanel((Rectangle){ state->anchorEdit.x, state->anchorEdit.y, 168, 40 }, NULL);
    GuiPanel((Rectangle){ state->anchorTools.x, state->anchorTools.y, state->anchorVisuals.x - state->anchorTools.x + 1, 40 }, NULL);
    GuiPanel((Rectangle){ state->anchorVisuals.x, state->anchorVisuals.y, 220, 40 }, NULL);
    GuiPanel((Rectangle){ state->anchorRight.x, state->anchorRight.y, 104, 40 }, NULL);

    // Project/File options
    // GuiSetTooltip("Create new icons project (LCTRL+N)");
    // state->btnNewFilePressed = GuiButton((Rectangle){ state->anchorFile.x + 12, state->anchorFile.y + 8, 24, 24 }, "#8#");
    // GuiSetTooltip("Load icons .rgi file (LCTRL+O)");
    // state->btnLoadFilePressed = GuiButton((Rectangle){ state->anchorFile.x + 12 + 24 + 4, state->anchorFile.y + 8, 24, 24 }, "#5#");
    // GuiSetTooltip("Save icons .rgi file (LCTRL+S)");
    // state->btnSaveFilePressed = GuiButton((Rectangle){ state->anchorFile.x + 12 + 48 + 8, state->anchorFile.y + 8, 24, 24 }, "#6#");
    // GuiSetTooltip("Export icons file (LCTRL+E)");
    // state->btnExportFilePressed = GuiButton((Rectangle){ state->anchorFile.x + 12 + 72 + 12, state->anchorFile.y + 8, 24, 24 }, "#7#");

    // Edit options
    // GuiSetTooltip("Cut selected icon (LCTRL+X)");
    // state->btnCutPressed = GuiButton((Rectangle){ state->anchorEdit.x + 12, state->anchorEdit.y + 8, 24, 24 }, "#17#");             // Cut
    // GuiSetTooltip("Copy selected icon (LCTRL+C)");
    // state->btnCopyPressed = GuiButton((Rectangle){ state->anchorEdit.x + 12 + 24 + 4, state->anchorEdit.y + 8, 24, 24 }, "#16#");   // Copy
    // GuiSetTooltip("Paste icon previously cut/copied (LCTRL+V)");
    // state->btnPastePressed = GuiButton((Rectangle){ state->anchorEdit.x + 12 + 48 + 8, state->anchorEdit.y + 8, 24, 24 }, "#18#");  // Paste
    // GuiGroupBox((Rectangle){ state->anchorEdit.x + 12 + 72 + 16, state->anchorEdit.y + 8, 24, 24 }, NULL);
    // GuiSetTooltip("Clear previously cut/copied icon");
    // state->btnCleanPressed = GuiButton((Rectangle){ state->anchorEdit.x + 12 + 96 + 24, state->anchorEdit.y + 8, 24, 24 }, "#079#");  // Clean

    // Visuals options
    // GuiLabel((Rectangle){ state->anchorVisuals.x + 10, state->anchorVisuals.y + 8, 60, 24 }, "Style:");
    // GuiSetTooltip("Select visual UI style");
    // GuiSetStyle(COMBOBOX, COMBO_BUTTON_WIDTH, 42);
    // GuiComboBox((Rectangle){ state->anchorVisuals.x + 8 + 48, state->anchorVisuals.y + 8, 120, 24 }, "Light;Jungle;Candy;Lavanda;Cyber;Terminal;Ashes;Bluish;Dark;Cherry;Sunny;Enefete;Amber;Genesis", &state->visualStyleActive);
    // GuiSetStyle(COMBOBOX, COMBO_BUTTON_WIDTH, 36);

    // Info options
    // GuiSetTooltip("Show help window (F1)");
    // state->btnHelpPressed = GuiButton((Rectangle){ state->anchorRight.x + (screenWidth - state->anchorRight.x) - 12 - 72 - 8, state->anchorRight.y + 8, 24, 24 }, "#221#"); 
    // GuiSetTooltip("Show info window (F2)");
    // state->btnAboutPressed = GuiButton((Rectangle){ state->anchorRight.x + (screenWidth - state->anchorRight.x) - 12 - 48 - 4, state->anchorRight.y + 8, 24, 24 }, "#222#");
    // GuiSetTooltip("Report an issue (F3)");
    // state->btnIssuePressed = GuiButton((Rectangle){ state->anchorRight.x + (screenWidth - state->anchorRight.x) - 12 - 24, state->anchorRight.y + 8, 24, 24 }, "#220#");

    GuiSetTooltip(NULL);
}

#endif // GUI_MAIN_TOOLBAR_IMPLEMENTATION