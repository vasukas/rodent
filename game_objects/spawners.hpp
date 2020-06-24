#ifndef SPAWNERS_HPP
#define SPAWNERS_HPP

class  Entity;
class  GameCore;
struct LevelTerrain;

void level_spawn(GameCore& core, LevelTerrain& lt);

LevelTerrain* drone_test_terrain();
void drone_test_spawn(GameCore& core, LevelTerrain& lt);

LevelTerrain* survival_terrain();
void survival_spawn(GameCore& core, LevelTerrain& lt);

// Uses wall data from LevelControl, so should be used before spawning any objects
void lightmap_spawn(GameCore& core, const char *filename, Entity& walls);

#endif // SPAWNERS_HPP
