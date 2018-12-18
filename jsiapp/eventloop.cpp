#include "stdafx.h"

#include "eventloop.h"

#include <algorithm>

#include <chrono>

#include <iostream>

EventLoop::EventLoop() {

}

void EventLoop::threadProc() {
	
	while (true) {
		iteration();
	}
}

void EventLoop::loop() {
	t_ = std::move(std::thread(&EventLoop::threadProc, this));
}

void EventLoop::iteration()
{
	std::this_thread::sleep_for(std::chrono::seconds(2));
	std::cout << "JSI loop : iter\n";
  
	
	while (!_taskQueue.empty()) {
		std::cout << "JSI loop : task pop\n";
		auto func = _taskQueue.front();
		_taskQueue.pop();
		func();
  }

  while (!_jsiBgCompletedItems.empty()) {
    std::cout << "JSI loop : bg task pop\n";
    std::shared_ptr<BgJsiTask> bgEvent = _jsiBgCompletedItems.front();
    _jsiBgCompletedItems.pop();

    facebook::jsi::Value result = facebook::jsi::valueFromDynamic(bgEvent->runtime, bgEvent->output);
    bgEvent->resolver.Resolve(bgEvent->runtime, result);
  }
	
	if (_timerEvents.size() == 0)
  {
    return;
  }

  std::cout << "JSI loop : timer event\n";

  std::chrono::time_point<std::chrono::steady_clock> next = _timerEvents.begin()->next_;
  std::chrono::milliseconds delay = _timerEvents.begin()->delay_;

  if (std::chrono::steady_clock::now() >= next + delay)
  {
	bool isPeriodic = _timerEvents.begin()->periodic_;
	_currentTimerId = _timerEvents.begin()->timerId_;
	
	std::unique_ptr<JSIFunctionProxy> jsiFuncProxy;
	facebook::jsi::Function jsiFunc = std::move(_timerEvents.begin()->jsiFunctionProxy_->jsiFunc_);
	facebook::jsi::Runtime& rt = _timerEvents.begin()->jsiFunctionProxy_->jsiRuntime_;
	
    _timerEvents.erase(_timerEvents.begin());

    jsiFunc.call(rt, nullptr, 0);

    if (isPeriodic && _currentTimerId > 0)
    {
      _timerEvents.insert(TimerEvent{ next + delay, delay, std::make_unique<JSIFunctionProxy>(rt, std::move(jsiFunc)), _currentTimerId, isPeriodic });
    }
    else
    {
      //TODO: releaseBackToPool(periodicTimer.timerId);
    }

    _currentTimerId = 0;
  }
}

size_t GetUnusedTimerId() noexcept
{
  // TODO: reuse timer ids?
  static size_t timerId{ 1 };
  return timerId++;
}

void EventLoop::add(std::shared_ptr<BgJsiTask> jsiBgItem) {
	_jsiBgCompletedItems.push(jsiBgItem);
}
//
//void EventLoop::add(std::unique_ptr<AsyncEvent> asyncEvent)
//{
//	_asyncEvents.emplace(std::move(asyncEvent));
//}

// Parameter could be size_t instead of double, but this makes it easy to pass the js number through
size_t EventLoop::add(const std::chrono::milliseconds& delay, std::unique_ptr<JSIFunctionProxy>&& handler)
{
  size_t timerId = GetUnusedTimerId();
  _timerEvents.emplace(TimerEvent{ std::chrono::steady_clock::now(), delay, std::move(handler), timerId, false });
  return timerId;
}

size_t EventLoop::addPeriodic(const std::chrono::milliseconds& delay, std::unique_ptr<JSIFunctionProxy>&& handler)
{
  size_t timerId = GetUnusedTimerId();
  _timerEvents.emplace(TimerEvent{ std::chrono::steady_clock::now(), delay, std::move(handler), timerId, true });
  return timerId;
}

void EventLoop::cancel(size_t timerId)
{
  if (_currentTimerId == timerId)
  {
    // Cancelling the current timer during its own handler execution
    _currentTimerId = 0;
    return;
  }

  auto it = std::find_if(_timerEvents.begin(), _timerEvents.end(), [&](const TimerEvent& te) noexcept { return te.timerId_ == timerId; });
  if (it != _timerEvents.end())
  {
    _timerEvents.erase(it);
  }
}
