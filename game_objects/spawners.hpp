#ifndef SPAWNERS_HPP
#define SPAWNERS_HPP

class  GameCore;
struct LevelTerrain;

void level_spawn(GameCore& core, LevelTerrain& lt);
void level_spawn_debug(GameCore& core, LevelTerrain& lt);

#endif // SPAWNERS_HPP
