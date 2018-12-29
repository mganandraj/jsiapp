#include "stdafx.h"

#include "eventloop.h"

#include <algorithm>

#include <chrono>

#include <iostream>

#include "scripthost.h"

BgEventLoop::BgEventLoop() {

}

void BgEventLoop::threadProc() {

	while (true) {
		iteration();
	}
}

void BgEventLoop::loop() {
	// t_ = std::move(std::thread(&BgEventLoop::threadProc, this));
}

void BgEventLoop::iteration()
{
	std::this_thread::sleep_for(std::chrono::seconds(2));
	std::cout << "Bg Event Loop : iter\n";


	while (!_taskQueue.empty()) {
		std::cout << "Bg Event Loop : bg task pop\n";
		std::shared_ptr<BgJsiTask> bgEvent = _taskQueue.front();
		_taskQueue.pop();
		
		bgEvent->output = bgEvent->func(bgEvent->input);

    bgEvent->jsiEventLoop->add(bgEvent);
	}
}

void BgEventLoop::add(std::shared_ptr<BgJsiTask> handler)
{
	_taskQueue.push(handler);
}