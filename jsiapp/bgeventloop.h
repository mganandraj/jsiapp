#pragma once

#include <chrono>
#include <set>
#include <functional>
#include <jsi.h>
#include <future>
#include <thread>

#include <queue>


struct EventLoop
{
	void iteration();

	std::thread t_;
	void threadProc();
	void loop();

	// Parameter could be size_t instead of double, but this makes it easy to pass the js number through
	size_t add(const std::chrono::milliseconds& delay, std::unique_ptr<JSIFunctionProxy>&& handler);
	
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