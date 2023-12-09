#pragma once

#include "Color.hpp"
#include <map>
#include <cmath>
#include <cstdlib>
#include <memory>

static inline float rand_f(float min, float max)
{
  return min + static_cast<float>(rand()) / ( static_cast<float>(RAND_MAX)/(max-min));
}

class Scene
{
public:
  virtual ~Scene() = default;
  virtual void update(LEDBuffer& buffer, float deltaTime, float param) = 0;
protected:
  Scene() = default;
};

using SceneCollection = std::vector<std::unique_ptr<Scene>>;
SceneCollection Scenes;
std::vector<std::string> SceneNames;
template <typename T>
struct SceneTypeRegistration
{
  SceneTypeRegistration<T>(std::string name) 
  {
    SceneNames.push_back(name);
    Scenes.push_back(std::make_unique<T>());
  }
};
#define RegisterScene(T) static SceneTypeRegistration<T> _registration##T(#T)

class WarmWhite : public Scene
{
public:
  virtual void update(LEDBuffer& buffer, float /* deltaTime */, float param) override
  {
    float colorTempK = (param*7000.0f) + 2000.0f;
    auto color = GetColorFromTemperature(colorTempK);
    for (int i = 0; i < buffer.size(); ++i)
    {
      buffer[i] = color;
    }
  }
};
RegisterScene(WarmWhite);

class GamerRGB : public Scene
{
public:
  virtual void update(LEDBuffer& buffer, float deltaTime, float param) override
  {
    t = fmodf(t + deltaTime, 10.0f);
    float baseHue = t * 36.0f;
    for (int i = 0; i < buffer.size(); ++i)
    {
      float locationOffsetHue = (float)i * (360.0f / (float)buffer.size());
      buffer[i] = HSVColor{ fmodf(baseHue + locationOffsetHue, 360.0f) , 1.0f, 1.0f }.toRGB();
    }
  }
private:
  float t = 0.0f;
};
RegisterScene(GamerRGB);

class Halloween : public Scene
{
public:
  Halloween()
  {
    srand(349875232);
    t_ = fadeTime;
  }
  ~Halloween() = default;

  virtual void update(LEDBuffer& buffer, float deltaTime, float param) override
  {
    // Make sure these guys are the right size!
    src_.resize(buffer.size());
    dst_.resize(buffer.size());

    t_ += deltaTime;
    if (t_ >= fadeTime)
    {
      dst_.swap(src_);
      generateColors(dst_);
    }
    while (t_ >= fadeTime)
    {
      t_ -= fadeTime;
    }
    float tParam = t_ / fadeTime;
    for (int i=0; i < buffer.size(); ++i)
    {
      buffer[i] = RGBColor::blend(src_[i], dst_[i], tParam);
    }
  }

  void generateColors(std::vector<RGBColor>& arr)
  {
    for (int i=0; i < arr.size(); ++i)
    {
      float hue = rand_f(10.0, 20.0);
      float saturation = rand_f(0.9f, 1.0f);
      float brightness = rand_f(0.3f, 0.7f);
      arr[i] = HSVColor{hue, saturation, brightness}.toRGB();
    }
  }

private:
  float t_ = 0;
  std::vector<RGBColor> src_;
  std::vector<RGBColor> dst_;
  float fadeTime = 4.0f;
};
RegisterScene(Halloween);

class PureColor : public Scene
{
public:
  virtual void update(LEDBuffer& buffer, float /* deltaTime */, float param) override
  {
    float hue = param * 360.0f;
    auto color = HSVColor{hue, 1.0f, 1.0f}.toRGB();
    for (int i = 0; i < buffer.size(); ++i)
    {
      buffer[i] = color;
    }
  }
};
RegisterScene(PureColor);

class CandyCane : public Scene
{
public:
  virtual void update(LEDBuffer& buffer, float /* deltaTime */, float param) override
  {
    int spacing = std::round(param * 20.0f) + 1.0f;
    spacing += 1;
    RGBColor colors[2] = {{230, 30, 0}, {86, 86, 86}};
    for (int i = 0; i < buffer.size(); ++i)
    {
      buffer[i] = colors[(i/spacing)%2];
    }
  }
};
RegisterScene(CandyCane);

class ChristmasStripes : public Scene
{
public:
  virtual void update(LEDBuffer& buffer, float /* deltaTime */, float param) override
  {
    int spacing = std::round(param * 20.0f) + 1.0f;
    spacing += 1;
    RGBColor colors[2] = {{230, 30, 0}, {0, 230, 30}};
    for (int i = 0; i < buffer.size(); ++i)
    {
      buffer[i] = colors[(i/spacing)%2];
    }
  }
};
RegisterScene(ChristmasStripes);