#pragma once

template<typename T>
using ScrollComponentDrawItem = Vector2 (*)(Vector2 pos, bool show, const T& item, bool isSelected, bool* isClicked);

template<typename T>
struct ScrollComponent {
    Rectangle rect = { 0, 0, 0, 0 };
    const char* title = nullptr;
    const char* emptyText = "No items";
    ScrollComponentDrawItem<T> drawItem = nullptr;

    std::vector<T> items;

    int selected = -1;
    bool init = false;

    Rectangle view = { 0, 0, 0, 0 };
    Vector2 scroll = { 0, 0 };
    Rectangle contentRec = { 0, 0, 0, 0 };
};

template<typename T>
void GuiCustomScrollPanel(ScrollComponent<T>& component) {
    if(component.items.size() > 0) {
        int totalY = 0;
        std::vector<Vector2> itemSizes;
        for (size_t i = 0; i < component.items.size(); i++){
            Vector2 size = component.drawItem((Vector2){ 0, 0 }, false, component.items[i], false, nullptr);
            totalY += size.y;
            itemSizes.push_back(size);
        }

        component.contentRec = { component.rect.x, component.rect.y + 20, component.rect.width - 15, totalY };
        GuiScrollPanel(component.rect, component.title, component.contentRec, &component.scroll, &component.view);
        BeginScissorMode(component.view.x, component.view.y, component.view.width, component.view.height);
            int y = 10;
            for (size_t i = 0; i < component.items.size(); i++){
                bool isClicked = false;
                bool isSelected = component.selected == (int)i;
                Vector2 pos = { component.contentRec.x + component.scroll.x, component.contentRec.y + y + component.scroll.y };
                bool isVisible = pos.y + itemSizes[i].y >= component.view.y && pos.y <= component.view.y + component.view.height;
                if(!isVisible) {
                    y += itemSizes[i].y;
                    continue;
                }
                Vector2 size = component.drawItem(pos, true, component.items[i], isSelected, &isClicked);
                y += size.y;
                if(isClicked) {
                    component.selected = component.selected == (int)i ? -1 : (int)i;
                }
            }
        EndScissorMode();
    } else {
        GuiPanel(component.rect, component.emptyText);
    }
}