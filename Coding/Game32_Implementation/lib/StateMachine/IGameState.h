#ifndef I_GAME_STATE_H
#define I_GAME_STATE_H

class IGameState {
public:
    virtual ~IGameState() = default;

    virtual void onEnter() = 0;
    virtual void onUpdate() = 0;
    virtual void onDraw() = 0;
    virtual void onExit() = 0;
};

#endif // I_GAME_STATE_H