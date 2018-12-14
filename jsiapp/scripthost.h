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

  double setTimeout(std::function<void(double) noexcept>&& callback, double ms);
  double setImmediate(std::function<void(double) noexcept>&& callback);
  double setInterval(std::function<void(double) noexcept>&& callback, double delay);
  void clearTimeout(double timeoutId);
};