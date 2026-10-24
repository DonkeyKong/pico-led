#pragma once

#include <cpp/Color.hpp>
#include <cpp/LedStripWs2812b.hpp>
#include <map>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <functional>

class Scene;

using SceneCollection = std::vector<Scene*>;
SceneCollection Scenes;
std::vector<std::string> SceneNames;

class Scene
{
public:
  using UpdateFunc = std::function<void(LEDBuffer& buffer, float deltaTime, float param, bool firstFrame)>;
  UpdateFunc updateFunc_;

  Scene(std::string name, UpdateFunc updateFunc) : updateFunc_ {updateFunc}
  {
    SceneNames.push_back(name);
    Scenes.push_back(this);
  }

  void update(LEDBuffer& buffer, float deltaTime, float param, bool firstFrame)
  {
    updateFunc_(buffer, deltaTime, param, firstFrame);
  }
};