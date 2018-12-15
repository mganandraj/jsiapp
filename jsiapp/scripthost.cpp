#include "stdafx.h"

#include <string>

#include "scripthost.h"

#include "V8Runtime.h"

#include "typedjsi.h"

using namespace facebook;

class JSILogger {
public:
  static jsi::HostFunctionType getLogger() {
    std::shared_ptr<JSILogger> jsiLogger(new JSILogger());
    auto func = [jsiLogger](jsi::Runtime& runtime, const jsi::Value&, const jsi::Value* args, size_t count) {
      jsiLogger->ping();
      OutputDebugStringA(args[0].asString(runtime).utf8(runtime).c_str() );
      return jsi::Value::undefined();
    };
    return std::move(func);
  }

  ~JSILogger() {
    OutputDebugStringA("~JSILogger");
  }

  void ping() {
    OutputDebugStringA("ping ping ");
  }
};

class MyHostObject {
public:
  class HostObjectImpl : public jsi::HostObject {
  public:
    HostObjectImpl(std::shared_ptr<MyHostObject> hostObject)
      : hostObject_(hostObject) {}

    ~HostObjectImpl() {
      OutputDebugStringA("~HostObjectImpl");
    }

    jsi::Value get(jsi::Runtime& rt, const jsi::PropNameID& name) override {
      return jsi::String::createFromUtf8(rt, hostObject_->get(name.utf8(rt)));
    }

    void set(jsi::Runtime& rt, const jsi::PropNameID& name, const jsi::Value& value) override {
      hostObject_->set(name.utf8(rt), value.getString(rt).utf8(rt));
    }

    std::shared_ptr<MyHostObject> hostObject_;
  };

  static std::shared_ptr<jsi::HostObject> getHostObject() {
    return std::make_shared<HostObjectImpl>(std::make_shared<MyHostObject>());
  }

  std::string get(const std::string& propName) {
    if (propName == "name") {
      return name_;
    }

    if (propName == "state") {
      return state_;
    }
  }

  void set(const std::string& propName, const std::string& value) {
    if (propName == "name") {
      name_.assign(value);
    }

    if (propName == "state") {
      state_.assign(value);
    }
  }

  ~MyHostObject() {
    OutputDebugStringA("~MyHostObject");
  }

  std::string name_{ "MyHostObject" };
  std::string state_{ "sleepy" };
};


ScriptHost::ScriptHost() 
  :runtime_(facebook::v8runtime::makeV8Runtime()) {
  
  runtime_->global().setProperty(
    *runtime_,
    "mylogger",
    jsi::Function::createFromHostFunction(
      *runtime_,
      jsi::PropNameID::forAscii(*runtime_, "mylogger"),
      1,
      JSILogger::getLogger()));

  runtime_->global().setProperty(
    *runtime_,
    "getTimeAsync",
    jsi::Function::createFromAsyncHostFunction(
      *runtime_,
      jsi::PropNameID::forAscii(*runtime_, "getTimeAsync"),
      1,
      [](jsi::Runtime& runtime, const jsi::Value&, const jsi::Value* args, size_t count) { 
          std::packaged_task<jsi::Value()> task([]() {
            return jsi::Value::undefined();
          });

          return task.get_future();
        }));

  runtime_->global().setProperty(
    *runtime_,
    "myobject",
    jsi::Object::createFromHostObject(*runtime_, MyHostObject::getHostObject())
  );

  runtime_->global().setProperty(*runtime_, "setTimeout",
    facebook::jsi::Function::createFromHostFunction(*runtime_, facebook::jsi::PropNameID::forAscii(*runtime_, "settimeout"), 2, [this](facebook::jsi::Runtime& runtime, const facebook::jsi::Value&, const facebook::jsi::Value* args, size_t count) {
    if (count != 2) {
      throw std::invalid_argument("Function setTimeout expects 2 arguments");
    }

    double returnvalue = this->setTimeout(args[0].getObject(runtime).asFunction(runtime), typedjsi::get<double>(runtime, args[1]) /*ms*/);
    return facebook::jsi::detail::toValue(runtime, returnvalue);
  }));

  runtime_->global().setProperty(*runtime_, "setImmediate",
    facebook::jsi::Function::createFromHostFunction(*runtime_, facebook::jsi::PropNameID::forAscii(*runtime_, "setImmediate"), 1, [this](facebook::jsi::Runtime& runtime, const facebook::jsi::Value&, const facebook::jsi::Value* args, size_t count) {
    if (count != 1) {
      throw std::invalid_argument("Function setImmediate expects 1 arguments");
    }

    double returnValue = this->setImmediate(args[0].getObject(runtime).asFunction(runtime));
    return facebook::jsi::detail::toValue(runtime, returnValue);
  }));

  runtime_->global().setProperty(*runtime_, "setInterval",
    facebook::jsi::Function::createFromHostFunction(*runtime_, facebook::jsi::PropNameID::forAscii(*runtime_, "setInterval"), 2, [this](facebook::jsi::Runtime& runtime, const facebook::jsi::Value&, const facebook::jsi::Value* args, size_t count) {
    if (count != 2) {
      throw std::invalid_argument("Function setInterval expects 2 arguments");
    }

    double returnValue = this->setInterval(args[0].getObject(runtime).asFunction(runtime), typedjsi::get<double>(runtime, args[1]) /*delay*/);
    return facebook::jsi::detail::toValue(runtime, returnValue);
  }));

  runtime_->global().setProperty(*runtime_, "clearTimeout",
    facebook::jsi::Function::createFromHostFunction(*runtime_, facebook::jsi::PropNameID::forAscii(*runtime_, "clearTimeout"), 1, [this](facebook::jsi::Runtime& runtime, const facebook::jsi::Value&, const facebook::jsi::Value* args, size_t count) {
    if (count != 1) {
      throw std::invalid_argument("Function clearTimeout expects 1 arguments");
    }

    this->clearTimeout(typedjsi::get<double>(runtime, args[0]) /*timeoutId*/);

    return facebook::jsi::Value::undefined();
  }));

}

struct StringBuffer : public jsi::Buffer {
  size_t size() const override {
    return string_.size();
  }

  const uint8_t* data() const override {
    return reinterpret_cast<const uint8_t*>(string_.c_str());
  }

  StringBuffer(std::string& str)
    : string_(str) {}
    
  std::string string_;

  /*std::string javascript_ = ""
    "print('hello v8...');"
    "//mylogger('hello v8 ... its me ...');"
    "//throw 10;";*/
};

void ScriptHost::runScript(std::string& script) {
  try{
    std::string sourceUrl("MyJS");
    runtime_->evaluateJavaScript(std::make_unique<StringBuffer>(script), sourceUrl);
  }
  catch (std::exception& exc) {
    OutputDebugStringA(exc.what());
  }
}

double ScriptHost::setTimeout(jsi::Function&& jsiFuncCallback, double ms)
{
  auto timeout = std::chrono::milliseconds(static_cast<long long>(ms));
  auto timerId = eventLoop_.add(timeout, std::make_unique<JSIFunctionProxy>(*runtime_, std::move(jsiFuncCallback)));
  return static_cast<double>(timerId);
}

double ScriptHost::setInterval(jsi::Function&& jsiFuncCallback, double delay)
{
  auto timeout = std::chrono::milliseconds(static_cast<long long>(delay));
  auto timerId = eventLoop_.addPeriodic(timeout, std::make_unique<JSIFunctionProxy>(*runtime_, std::move(jsiFuncCallback)));
  return static_cast<double>(timerId);
}

double ScriptHost::setImmediate(jsi::Function&& jsiFuncCallback)
{
  try
  {
    auto timerId = eventLoop_.add(std::chrono::milliseconds(0), std::make_unique<JSIFunctionProxy>(*runtime_, std::move(jsiFuncCallback)));
    return static_cast<double>(timerId);
  }
  catch (...)
  {
    std::throw_with_nested(facebook::jsi::JSError(*runtime_, "Failed to register native callback."));
  }
}

void ScriptHost::clearTimeout(double timerId)
{
  eventLoop_.cancel(static_cast<size_t>(timerId));
}
