#pragma once

#include <memory>

#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"

class ViewManager;

// A single screen of the UI, swapped in through the ViewManager.
class View {
public:
    virtual ~View() = default;

    virtual void Init() {}
    virtual void Update() {}
    virtual void Render() = 0;

    void InternalInit(const std::shared_ptr<ViewManager>& manager) {
        // Weak to avoid a cycle: the ViewManager owns the View.
        this->mManager = manager;
        this->Init();
    }

protected:
    std::shared_ptr<ViewManager> Manager() const {
        return mManager.lock();
    }

private:
    std::weak_ptr<ViewManager> mManager;
};

// Owns the active View and drives its per-frame Update/Render.
class ViewManager : public std::enable_shared_from_this<ViewManager> {
public:
    void SetView(const std::shared_ptr<View>& view) {
        mCurrent = view;
        if (mCurrent != nullptr) {
            mCurrent->InternalInit(shared_from_this());
        }
    }

    const std::shared_ptr<View>& Current() const {
        return mCurrent;
    }

    void Render() {
        if (mCurrent != nullptr) {
            mCurrent->Update();
            mCurrent->Render();
        }
    }

private:
    std::shared_ptr<View> mCurrent = nullptr;
};
