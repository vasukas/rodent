#ifndef TUTORIAL_HPP
#define TUTORIAL_HPP

class  GameCore;
struct LevelTerrain;

LevelTerrain* tutorial_terrain();
void tutorial_spawn(GameCore& core, LevelTerrain& lt);

#endif // TUTORIAL_HPP
