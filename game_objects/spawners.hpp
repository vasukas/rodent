#ifndef SPAWNERS_HPP
#define SPAWNERS_HPP

class  GameCore;
struct LevelTerrain;

void level_spawn(GameCore& core, LevelTerrain& lt);

LevelTerrain* drone_test_terrain();
void drone_test_spawn(GameCore& core, LevelTerrain& lt);

LevelTerrain* survival_terrain();
void survival_spawn(GameCore& core, LevelTerrain& lt);

#endif // SPAWNERS_HPP
