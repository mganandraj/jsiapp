#include "stdafx.h"
#include "scripthost.h"
#include <iostream>
#include "JSIDynamic.h"

using namespace facebook;

int main()
{

	// ScriptHost::instance() ScriptHost::instance();
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

  /*ScriptHost::instance().jsiEventLoop_._taskQueue.push([&ScriptHost::instance()]() {
     facebook::jsi::Function func = ScriptHost::instance().runtime_->global().getPropertyAsFunction(*ScriptHost::instance().runtime_, "getData");
     jsi::Value ret = func.call(*ScriptHost::instance().runtime_, nullptr, 0);
     jsi::Promise promise = ret.getObject(*ScriptHost::instance().runtime_).getPromise(*ScriptHost::instance().runtime_);
       
     {
       facebook::jsi::Function laterFunc = ScriptHost::instance().runtime_->global().getPropertyAsFunction(*ScriptHost::instance().runtime_, "later");
       jsi::Promise res = promise.Then(*ScriptHost::instance().runtime_, laterFunc);
     }

     {
       facebook::jsi::Function catchFunc = ScriptHost::instance().runtime_->global().getPropertyAsFunction(*ScriptHost::instance().runtime_, "catchh");
       jsi::Promise res2 = promise.Catch(*ScriptHost::instance().runtime_, catchFunc);
     }
  });*/

  ScriptHost::instance().jsiEventLoop_._taskQueue.push([]() {
    facebook::jsi::Function func = jsi::Function::createFromHostFunction(*ScriptHost::instance().runtime_, jsi::PropNameID::forAscii(*ScriptHost::instance().runtime_, "getDataAsync"),
      1, 
      [](jsi::Runtime& runtime, const jsi::Value&, const jsi::Value* args, size_t count) {
        assert(count == 1);
        
        jsi::PromiseResolver resolver = jsi::PromiseResolver::create(*ScriptHost::instance().runtime_);

        folly::dynamic arg = facebook::jsi::dynamicFromValue(*ScriptHost::instance().runtime_, args[0]);

        jsi::Value promise = resolver.getPromise(*ScriptHost::instance().runtime_);

        std::shared_ptr<BgJsiTask> task = std::make_shared<BgJsiTask>(*ScriptHost::instance().runtime_, std::move(resolver), &ScriptHost::instance().jsiEventLoop_);
        task->input = arg;
        task->func = [](folly::dynamic& input) {return input; };

        ScriptHost::instance().bgEventLoop_.add(task);

		    return promise;
    });

    ScriptHost::instance().runtime_->global().setProperty(*ScriptHost::instance().runtime_, jsi::PropNameID::forAscii(*ScriptHost::instance().runtime_, "getDataAsync"), func);

  });

  // std::string script("var promise = getDataAsync('xyz'); print('Hoy');promise.then(function(value) { print(value);}); //# sourceURL=filename.js");
  std::string script("print('HOYHOYHOY'); //# sourceURL=filename.js");
  ScriptHost::instance().runScript(script);

  ScriptHost::instance().jsiEventLoop_.t_.join();
}