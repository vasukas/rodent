class LevelTerrain;
class GameCore;
class GameModeCtr;

#include <string>

struct MPTEST {
    static MPTEST* make(const char *addr, const char *port, bool is_server);
    virtual LevelTerrain* terrain() = 0; // + connect
    virtual void spawn(GameCore& core, LevelTerrain& lt) = 0;
    virtual GameModeCtr* mode() = 0; // not owned internally
};

