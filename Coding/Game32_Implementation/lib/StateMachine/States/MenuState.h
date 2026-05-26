#ifndef MENU_STATE_H
#define MENU_STATE_H

#include "../IGameState.h"
#include <vector>
#include <string>

class MenuState : public IGameState {
public:
    static MenuState& getInstance();
    
    void onEnter() override;
    void onUpdate() override;
    void onDraw() override;
    void onExit() override;

private:
    MenuState() = default;
    
    void loadRoot();
    void loadDirectory(const std::string& path);

    std::vector<std::string> m_gamesList;
    int m_cursorIndex = 0;
    int m_topVisibleIndex = 0;
    const int MAX_VISIBLE_ITEMS = 7;
    
    bool m_inRoot = true;
    std::string m_currentPath = "";
};

#endif // MENU_STATE_H
