#pragma once

#include <chrono>
#include <set>
#include <functional>
#include <jsi.h>
#include <future>
#include <thread>

#include <queue>

struct JSIFunctionProxy {
  void operator()() {
    jsiFunc_.call(jsiRuntime_);
  }

  facebook::jsi::Function jsiFunc_;
  facebook::jsi::Runtime& jsiRuntime_;

  JSIFunctionProxy(facebook::jsi::Runtime& rt, facebook::jsi::Function&& func)
    : jsiFunc_(std::move(func)), jsiRuntime_(rt) {}
};

struct AsyncTaskProxy {
	void operator()() {
		asyncTask_();
	}

	std::packaged_task<facebook::jsi::Value()> asyncTask_;

	AsyncTaskProxy(std::packaged_task<facebook::jsi::Value()>&& asyncTask)
		: asyncTask_(std::move(asyncTask)){}
};

struct AsyncEvent
{
	std::promise<facebook::jsi::Value> promise_;
	std::function<facebook::jsi::Value()> func_;
};

// Simple "event loop" implementation to experiment with asynchronous code
struct TimerEvent
{
  //TODO: move-only TimerEvent(const TimerEvent&) =delete;
  std::chrono::time_point<std::chrono::steady_clock> next_;
  std::chrono::milliseconds delay_;
  std::unique_ptr<JSIFunctionProxy> jsiFunctionProxy_;
  size_t timerId_;
  bool periodic_;
};

inline bool operator<(const TimerEvent& l, const TimerEvent& r) noexcept
{
  return (l.next_ + l.delay_) < (r.next_ + r.delay_);
}

struct EventLoop
{
  void iteration();

  std::thread t_;
  void threadProc();
  void loop();

  // Parameter could be size_t instead of double, but this makes it easy to pass the js number through
  size_t add(const std::chrono::milliseconds& delay, std::unique_ptr<JSIFunctionProxy>&& handler);
  size_t addPeriodic(const std::chrono::milliseconds& delay, std::unique_ptr<JSIFunctionProxy>&& handler);

  void add(std::unique_ptr<AsyncEvent> asyncEvent);

  void cancel(size_t timerId);

  EventLoop();
    
  // Sorted queue that has the next timeout first
  std::multiset<TimerEvent> _timerEvents;

  std::function<void()> _initFunc;

  std::queue<std::function<void()>> _taskQueue;

  std::queue<std::function<void(jsi::Runtime& rt, const jsi::Value& thisVal, const jsi::Value* args, size_t count, jsi::PromiseResolver&&)>> _asyncCallQueue;

  std::multiset<std::unique_ptr<AsyncEvent>> _asyncEvents;

  // Holds the currently running timerId; goes to 0 if timer was cleared in its own handler
  size_t _currentTimerId;

};