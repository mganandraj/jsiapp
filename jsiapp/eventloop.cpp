#include "stdafx.h"

#include "eventloop.h"

#include <algorithm>

void EventLoop::loop()
{
  if (_timerEvents.size() == 0)
  {
    return;
  }

  std::chrono::time_point<std::chrono::steady_clock> next = _timerEvents.begin()->next;
  std::chrono::milliseconds delay = _timerEvents.begin()->delay;

  if (std::chrono::steady_clock::now() >= next + delay)
  {

    _currentTimerId = _timerEvents.begin()->timerId;
    std::unique_ptr<JSIFunctionProxy> jsiFuncProxy;
    
    facebook::jsi::Function jsiFunc = std::move(_timerEvents.begin()->jsiFunctionProxy->jsiFunc_);
    facebook::jsi::Runtime& rt = _timerEvents.begin()->jsiFunctionProxy->jsiRuntime_;

    bool isPeriodic = _timerEvents.begin()->periodic;

    // timerEvent.handler(static_cast<double>(timerEvent.timerId));
    // std::unique_ptr<JSIFunctionProxy> func = std::move(timerEvent.jsiFunctionProxy);
    // timerEvent.jsiFunctionProxy->jsiFunc_.call(timerEvent.jsiFunctionProxy->jsiRuntime_, nullptr, 0);

    // auto timerEvent = *_timerEvents.begin(); // TODO: can we std::move out of multiset? ::extract is C++17
    _timerEvents.erase(_timerEvents.begin());

    jsiFunc.call(rt, nullptr, 0);

    // _currentTimerId = timerEvent.timerId;
    // timerEvent.handler(static_cast<double>(timerEvent.timerId));
    // std::unique_ptr<JSIFunctionProxy> func = std::move(timerEvent.jsiFunctionProxy);
    // timerEvent.jsiFunctionProxy->jsiFunc_.call(timerEvent.jsiFunctionProxy->jsiRuntime_, nullptr, 0);

    if (isPeriodic && _currentTimerId > 0)
    {
      // timerEvent.next += timerEvent.delay;
      // _timerEvents.insert(std::move(timerEvent));
      _timerEvents.insert(TimerEvent{ next + delay, delay, std::make_unique<JSIFunctionProxy>(rt, std::move(jsiFunc)) , _currentTimerId, isPeriodic });
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

  auto it = std::find_if(_timerEvents.begin(), _timerEvents.end(), [&](const TimerEvent& te) noexcept { return te.timerId == timerId; });
  if (it != _timerEvents.end())
  {
    _timerEvents.erase(it);
  }
}
