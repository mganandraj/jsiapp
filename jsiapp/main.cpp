#include "stdafx.h"
#include "scripthost.h"
#include <iostream>
#include "JSIDynamic.h"

using namespace facebook;

int main()
{

	ScriptHost scriptHost;
	//std::string script("function getData() { "
	//	"return new Promise(function(resolve, reject) {"
//			  "var func = function(){ resolve('foo'); };"
	//		  "setImmediate(func); "
		//    "/*func();*/"/
//		"});"
	//"}"
//	"function later(arg) {print('later::' + arg)};"
	//"function catchh(arg) {print('catchh::' + arg)};"
	//"/*getData().then(later);*/");

  /*scriptHost.jsiEventLoop_._taskQueue.push([&scriptHost]() {
     facebook::jsi::Function func = scriptHost.runtime_->global().getPropertyAsFunction(*scriptHost.runtime_, "getData");
     jsi::Value ret = func.call(*scriptHost.runtime_, nullptr, 0);
     jsi::Promise promise = ret.getObject(*scriptHost.runtime_).getPromise(*scriptHost.runtime_);
       
     {
       facebook::jsi::Function laterFunc = scriptHost.runtime_->global().getPropertyAsFunction(*scriptHost.runtime_, "later");
       jsi::Promise res = promise.Then(*scriptHost.runtime_, laterFunc);
     }

     {
       facebook::jsi::Function catchFunc = scriptHost.runtime_->global().getPropertyAsFunction(*scriptHost.runtime_, "catchh");
       jsi::Promise res2 = promise.Catch(*scriptHost.runtime_, catchFunc);
     }
  });*/

  scriptHost.jsiEventLoop_._taskQueue.push([&scriptHost]() {
    facebook::jsi::Function func = jsi::Function::createFromHostFunction(*scriptHost.runtime_, jsi::PropNameID::forAscii(*scriptHost.runtime_, "getDataAsync"),
      1, 
      [&scriptHost](jsi::Runtime& runtime, const jsi::Value&, const jsi::Value* args, size_t count) {
        assert(count == 1);
        
        jsi::PromiseResolver resolver = jsi::PromiseResolver::create(*scriptHost.runtime_);

        folly::dynamic arg = facebook::jsi::dynamicFromValue(*scriptHost.runtime_, args[0]);

        jsi::Value promise = resolver.getPromise(*scriptHost.runtime_);

        std::shared_ptr<BgJsiTask> task = std::make_shared<BgJsiTask>(*scriptHost.runtime_, std::move(resolver), &scriptHost.jsiEventLoop_);
        task->input = arg;
        task->func = [](folly::dynamic& input) {return folly::dynamic("abcd"); };

        scriptHost.bgEventLoop_.add(task);

		    return promise;
    });

    scriptHost.runtime_->global().setProperty(*scriptHost.runtime_, jsi::PropNameID::forAscii(*scriptHost.runtime_, "getDataAsync"), func);

  });

  std::string script("var promise = getDataAsync([]); print('Hoy');promise.then(function(value) { print(value);});");
  scriptHost.runScript(script);

  scriptHost.jsiEventLoop_.t_.join();
}