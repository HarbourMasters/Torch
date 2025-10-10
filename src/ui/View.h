#pragma once

#include <memory>
#include "ui/ImGuiFileDialog.h"
#include "extras/IconsFontAwesome6.h"

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