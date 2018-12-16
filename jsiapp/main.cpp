#include "stdafx.h"
#include "scripthost.h"
#include <iostream>

using namespace facebook;

int main()
{

	ScriptHost scriptHost;
	std::string script("function getData() { "
		"print('1'); return new Promise(function(resolve, reject) {"
			"print('2'); "
		    "var func = function(){ print('3'); resolve('foo'); };"
			"setImmediate(func); "
		"});"
	"}"
	"function later() {print('later!!!')};"
	"function catchh() {print('catchhh!!!')};");

  //std::string script("function getData() { return 100;}",
	//"//setImmediate(function(){ mylogger('Hello'); });"
 //   "//mylogger('main');"
 //   "//mylogger('hello v8...');"
 //   "//mylogger('hello v8 ... its me ...');"
 //   "//throw 10;");

  scriptHost.runScript(script);

  scriptHost.eventLoop_.loop();

  scriptHost.eventLoop_._taskQueue.push([&scriptHost]() {
	  facebook::jsi::Function func = scriptHost.runtime_->global().getPropertyAsFunction(*scriptHost.runtime_, "getData");
	  jsi::Value ret = func.call(*scriptHost.runtime_, nullptr, 0);
	  jsi::Promise promise = ret.getObject(*scriptHost.runtime_).getPromise(*scriptHost.runtime_);
	 
	  facebook::jsi::Function laterFunc = scriptHost.runtime_->global().getPropertyAsFunction(*scriptHost.runtime_, "later");
	  promise.Then(*scriptHost.runtime_, laterFunc);

	  facebook::jsi::Function catchFunc = scriptHost.runtime_->global().getPropertyAsFunction(*scriptHost.runtime_, "catchh");
	  promise.Catch(*scriptHost.runtime_, catchFunc);
  });

  scriptHost.eventLoop_.t_.join();
}