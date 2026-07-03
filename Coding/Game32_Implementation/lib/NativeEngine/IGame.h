#ifndef IGAME_H
#define IGAME_H

class IGame {
public:
    virtual ~IGame() = default;
    virtual const char* getName() const = 0;
    virtual void setup() = 0;
    virtual void loop() = 0;
    virtual void onExit() {}
    virtual bool saveState(int slot) { return false; }
    virtual bool loadState(int slot) { return false; }
};

#endif // IGAME_H
