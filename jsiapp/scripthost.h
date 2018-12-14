#pragma once

#include <memory>
#include "eventloop.h"

#include "jsi.h"

class ScriptHost {
public:

  std::unique_ptr<facebook::jsi::Runtime> runtime_;
  EventLoop eventLoop_;

  ScriptHost();

  void runScript(std::string& script);

  double setTimeout(facebook::jsi::Function&& jsiFuncCallback, double ms);
  double setImmediate(facebook::jsi::Function&& jsiFuncCallback);
  double setInterval(facebook::jsi::Function&& jsiFuncCallback, double delay);
  void clearTimeout(double timeoutId);
};