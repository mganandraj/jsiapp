#pragma once

#include "stdafx.h"

#include "eventloop.h"

#include <algorithm>

void EventLoop::loop()
{
  if (_timerEvents.size() == 0)
  {
    return;
  }

  if (std::chrono::steady_clock::now() >= _timerEvents.begin()->next + _timerEvents.begin()->delay)
  {
    auto timerEvent = *_timerEvents.begin(); // TODO: can we std::move out of multiset? ::extract is C++17
    _timerEvents.erase(_timerEvents.begin());

    _currentTimerId = timerEvent.timerId;
    timerEvent.handler(static_cast<double>(timerEvent.timerId));

    if (timerEvent.periodic && _currentTimerId > 0)
    {
      timerEvent.next += timerEvent.delay;
      _timerEvents.insert(std::move(timerEvent));
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
size_t EventLoop::add(const std::chrono::milliseconds& delay, std::function<void(double) noexcept>&& handler)
{
  size_t timerId = GetUnusedTimerId();
  _timerEvents.emplace(TimerEvent{ std::chrono::steady_clock::now(), delay, std::move(handler), timerId, false });
  return timerId;
}

size_t EventLoop::addPeriodic(const std::chrono::milliseconds& delay, std::function<void(double) noexcept>&& handler)
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
