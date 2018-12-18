#include "stdafx.h"

#include "bgeventloop.h"

#include <algorithm>

#include <chrono>

#include <iostream>

BgEventLoop::BgEventLoop() {

}

void BgEventLoop::threadProc() {

	while (true) {
		iteration();
	}
}

void BgEventLoop::loop() {
	t_ = std::move(std::thread(&BgEventLoop::threadProc, this));
}

void BgEventLoop::iteration()
{
	std::this_thread::sleep_for(std::chrono::seconds(2));
	std::cout << "iter\n";


	while (!_taskQueue.empty()) {
		std::cout << "bg task pop\n";
		std::shared_ptr<BgJsiTask> bgEvent = _taskQueue.front();
		_taskQueue.pop();
		
		bgEvent->output = bgEvent->func(bgEvent->input);
	}

	//if (_timerEvents.size() == 0)
	//{
	//	return;
	//}

	//std::chrono::time_point<std::chrono::steady_clock> next = _timerEvents.begin()->next_;
	//std::chrono::milliseconds delay = _timerEvents.begin()->delay_;

	//if (std::chrono::steady_clock::now() >= next + delay)
	//{
	//	bool isPeriodic = _timerEvents.begin()->periodic_;
	//	_currentTimerId = _timerEvents.begin()->timerId_;

	//	std::unique_ptr<JSIFunctionProxy> jsiFuncProxy;
	//	facebook::jsi::Function jsiFunc = std::move(_timerEvents.begin()->jsiFunctionProxy_->jsiFunc_);
	//	facebook::jsi::Runtime& rt = _timerEvents.begin()->jsiFunctionProxy_->jsiRuntime_;

	//	_timerEvents.erase(_timerEvents.begin());

	//	jsiFunc.call(rt, nullptr, 0);

	//	if (isPeriodic && _currentTimerId > 0)
	//	{
	//		_timerEvents.insert(TimerEvent{ next + delay, delay, std::make_unique<JSIFunctionProxy>(rt, std::move(jsiFunc)), _currentTimerId, isPeriodic });
	//	}
	//	else
	//	{
	//		//TODO: releaseBackToPool(periodicTimer.timerId);
	//	}

	//	_currentTimerId = 0;
	//}
}

void BgEventLoop::add(std::shared_ptr<BgJsiTask> handler)
{
	_taskQueue.push(handler);
}