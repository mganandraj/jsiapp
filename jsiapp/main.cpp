#include "stdafx.h"
#include "scripthost.h"
#include <iostream>
#include "JSIDynamic.h"

using namespace facebook;

int main()
{

	ScriptHost scriptHost;
	std::string script("function getData() { "
		"print('1'); return new Promise(function(resolve, reject) {"
			"print('2'); "
		    "var func = function(){ print('3'); resolve('foo'); };"
			"/*setImmediate(func);*/ "
		    "func();"
		"});"
	"}"
	"function later(arg) {print(arg)};"
	"function catchh(arg) {print(arg)};"
	"/*getData().then(later);*/");

  //std::string script("function getData() { return 100;}",
	//"//setImmediate(function(){ mylogger('Hello'); });"
 //   "//mylogger('main');"
 //   "//mylogger('hello v8...');"
 //   "//mylogger('hello v8 ... its me ...');"
 //   "//throw 10;");

  scriptHost.runScript(script);

  scriptHost.jsiEventLoop_._taskQueue.push([&scriptHost]() {
	  facebook::jsi::Function func = scriptHost.runtime_->global().getPropertyAsFunction(*scriptHost.runtime_, "getData");
	  jsi::Value ret = func.call(*scriptHost.runtime_, nullptr, 0);
	  jsi::Promise promise = ret.getObject(*scriptHost.runtime_).getPromise(*scriptHost.runtime_);
    if (promise.isPending(*scriptHost.runtime_)) {
      facebook::jsi::Function laterFunc = scriptHost.runtime_->global().getPropertyAsFunction(*scriptHost.runtime_, "later");
      promise.Then(*scriptHost.runtime_, laterFunc);

      facebook::jsi::Function catchFunc = scriptHost.runtime_->global().getPropertyAsFunction(*scriptHost.runtime_, "catchh");
      promise.Catch(*scriptHost.runtime_, catchFunc);
    }
    else if (promise.isFulfilled(*scriptHost.runtime_)) {
      facebook::jsi::Function laterFunc = scriptHost.runtime_->global().getPropertyAsFunction(*scriptHost.runtime_, "later");
      std::vector<jsi::Value> vargs;
      vargs.push_back(std::move(promise.Result(*scriptHost.runtime_)));
      const jsi::Value* args = vargs.data();
      jsi::Value ret = laterFunc.call(*scriptHost.runtime_, args, vargs.size());
    }
    else {
      facebook::jsi::Function catchFunc = scriptHost.runtime_->global().getPropertyAsFunction(*scriptHost.runtime_, "catchh");
      std::vector<jsi::Value> vargs;
      vargs.push_back(std::move(promise.Result(*scriptHost.runtime_)));
      const jsi::Value* args = vargs.data();
      jsi::Value ret = catchFunc.call(*scriptHost.runtime_, args, vargs.size());
    }
  });

  scriptHost.jsiEventLoop_._taskQueue.push([&scriptHost]() {
    facebook::jsi::Function func = jsi::Function::createFromHostFunction(*scriptHost.runtime_, jsi::PropNameID::forAscii(*scriptHost.runtime_, "getDataAsync"),
      1, 
      [&scriptHost](jsi::Runtime& runtime, const jsi::Value&, const jsi::Value* args, size_t count) {
        assert(count == 1);
        
        jsi::PromiseResolver resolver = jsi::PromiseResolver::create(*scriptHost.runtime_);

        folly::dynamic arg = facebook::jsi::dynamicFromValue(*scriptHost.runtime_, args[0]);

        jsi::Value promise = resolver.getPromise(*scriptHost.runtime_);

		

		return promise;
    });

  });

  scriptHost.jsiEventLoop_.t_.join();
}