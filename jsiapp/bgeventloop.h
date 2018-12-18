#pragma once

#include <chrono>
#include <set>
#include <functional>
#include <jsi.h>
#include <future>
#include <thread>
#include "JSIDynamic.h"
#include <queue>

struct BgJsiTask {
  facebook::jsi::Runtime& runtime;
	facebook::jsi::PromiseResolver resolver;
	folly::dynamic input;
	folly::dynamic output;
	std::function<folly::dynamic(folly::dynamic)> func;
};

struct BgEventLoop
{
	void iteration();

	std::thread t_;
	void threadProc();
	void loop();

	void add(std::shared_ptr<BgJsiTask> handler);
	
	BgEventLoop();

	std::queue<std::shared_ptr<BgJsiTask>> _taskQueue;
};