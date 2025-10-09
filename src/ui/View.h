#pragma once

#include <memory>
#include <raylib.h>
#include "ui/raygui.h"

#undef RAYGUI_IMPLEMENTATION
#define GUI_WINDOW_FILE_DIALOG_IMPLEMENTATION
#include "ui/gui_file_dialogs.h"

class ViewManager;

class View {
public:
    virtual ~View() = default;

    virtual void Init() {}
    virtual void Update() {}
    virtual void Render() = 0;

    void InternalInit(std::shared_ptr<ViewManager> manager) {
        this->manager = manager;
        this->Init();
    }
private:
    std::shared_ptr<ViewManager> manager;
};

class ViewManager : public std::enable_shared_from_this<ViewManager> {
public:
    void SetView(std::shared_ptr<View> view) {
        if (current) {
            current->~View();
        }
        current = view;
        current->InternalInit(shared_from_this());
    }

    void Render() {
        if (current) {
            current->Update();
            current->Render();
        }
    }
private:
    std::shared_ptr<View> current = nullptr;
};