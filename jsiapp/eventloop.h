#pragma once

#include <chrono>
#include <set>
#include <functional>
#include <jsi.h>

struct JSIFunctionProxy {
  void operator()() {
    jsiFunc_.call(jsiRuntime_);
  }

  facebook::jsi::Function jsiFunc_;
  facebook::jsi::Runtime& jsiRuntime_;

  JSIFunctionProxy(facebook::jsi::Runtime& rt, facebook::jsi::Function&& func)
    : jsiFunc_(std::move(func)), jsiRuntime_(rt) {}
};

// Simple "event loop" implementation to experiment with asynchronous code
struct TimerEvent
{
  //TODO: move-only TimerEvent(const TimerEvent&) =delete;
  std::chrono::time_point<std::chrono::steady_clock> next;
  std::chrono::milliseconds delay;
  std::unique_ptr<JSIFunctionProxy> jsiFunctionProxy;
  size_t timerId;
  bool periodic;
};

inline bool operator<(const TimerEvent& l, const TimerEvent& r) noexcept
{
  return (l.next + l.delay) < (r.next + r.delay);
}

struct EventLoop
{
  void loop();

  // Parameter could be size_t instead of double, but this makes it easy to pass the js number through
  size_t add(const std::chrono::milliseconds& delay, std::unique_ptr<JSIFunctionProxy>&& handler);
  size_t addPeriodic(const std::chrono::milliseconds& delay, std::unique_ptr<JSIFunctionProxy>&& handler);

  void cancel(size_t timerId);

private:
  // Sorted queue that has the next timeout first
  std::multiset<TimerEvent> _timerEvents;

  // Holds the currently running timerId; goes to 0 if timer was cleared in its own handler
  size_t _currentTimerId;
};