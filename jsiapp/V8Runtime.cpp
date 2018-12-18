//  Copyright (c) Facebook, Inc. and its affiliates.
//
// This source code is licensed under the MIT license found in the
 // LICENSE file in the root directory of this source tree.

#include "stdafx.h"

#include "V8Runtime.h"

#include "v8.h"
#include "libplatform/libplatform.h"

#include <cstdlib>
#include <sstream>
#include <iostream>
#include <atomic>
#include <list>

#define _ISOLATE_CONTEXT_ENTER v8::Isolate *isolate = v8::Isolate::GetCurrent(); \
    v8::Isolate::Scope isolate_scope(isolate); \
    v8::HandleScope handle_scope(isolate); \
    v8::Context::Scope context_scope(context_.Get(isolate));

namespace facebook {
  namespace v8runtime {

    class V8Runtime : public jsi::Runtime {
    public:
      V8Runtime();
      ~V8Runtime();

      void evaluateJavaScript(std::unique_ptr<const jsi::Buffer> buffer, const std::string& sourceURL) override;

      jsi::Object global() override;

      std::string description() override;

      bool isInspectable() override;

    private:

      struct IHostProxy {
        virtual void destroy() = 0;
      };

      class HostObjectLifetimeTracker {
      public:
        void ResetHostObject(bool isGC /*whether the call is coming from GC*/) {
          assert(!isGC || !isReset_);
          if (!isReset_) {
            isReset_ = true;
            hostProxy_->destroy();
            objectTracker_.Reset();
          }
        }

        HostObjectLifetimeTracker(V8Runtime& runtime, v8::Local<v8::Object> obj, IHostProxy* hostProxy) : hostProxy_(hostProxy) {
          objectTracker_.Reset(runtime.GetIsolate(), obj);
          objectTracker_.SetWeak(this, HostObjectLifetimeTracker::Destroyed, v8::WeakCallbackType::kFinalizer);
        }

        // Useful for debugging.
        ~HostObjectLifetimeTracker() {
          assert(isReset_);
          std::cout << "~HostObjectLifetimeTracker" << std::endl;
        }

      private:
        v8::Persistent<v8::Object> objectTracker_;
        std::atomic<bool> isReset_{ false };
        IHostProxy* hostProxy_;

        static void Destroyed(const v8::WeakCallbackInfo<HostObjectLifetimeTracker>& data) {
          v8::HandleScope handle_scope(v8::Isolate::GetCurrent());
          data.GetParameter()->ResetHostObject(true /*isGC*/);
        }

      };

      class HostObjectProxy : public IHostProxy {
      public:
        static void Get(v8::Local<v8::Name> v8PropName, const v8::PropertyCallbackInfo<v8::Value>& info)
        {
          v8::Local<v8::External> data = v8::Local<v8::External>::Cast(info.This()->GetInternalField(0));
          HostObjectProxy* hostObjectProxy = reinterpret_cast<HostObjectProxy*>(data->Value());

          V8Runtime& runtime = hostObjectProxy->runtime_;
          std::shared_ptr<jsi::HostObject> hostObject = hostObjectProxy->hostObject_;

          v8::Local<v8::String> propNameStr = v8::Local<v8::String>::Cast(v8PropName);
          std::string propName;
          propName.resize(propNameStr->Utf8Length(info.GetIsolate()));
          propNameStr->WriteUtf8(info.GetIsolate(), &propName[0]);

          jsi::PropNameID propNameId = runtime.createPropNameIDFromUtf8(reinterpret_cast<uint8_t*>(&propName[0]), propName.length());
          info.GetReturnValue().Set(runtime.valueRef(hostObject->get(runtime, propNameId)));
        }

        static void Set(v8::Local<v8::Name> v8PropName, v8::Local<v8::Value> value, const v8::PropertyCallbackInfo<v8::Value>& info)
        {
          v8::Local<v8::External> data = v8::Local<v8::External>::Cast(info.Data());
          HostObjectProxy* hostObjectProxy = reinterpret_cast<HostObjectProxy*>(data->Value());

          V8Runtime& runtime = hostObjectProxy->runtime_;
          std::shared_ptr<jsi::HostObject> hostObject = hostObjectProxy->hostObject_;

          v8::Local<v8::String> propNameStr = v8::Local<v8::String>::Cast(v8PropName);

          std::string propName;
          propName.resize(propNameStr->Utf8Length(info.GetIsolate()));
          propNameStr->WriteUtf8(info.GetIsolate(), &propName[0]);

          hostObject->set(runtime, runtime.createPropNameIDFromUtf8(reinterpret_cast<uint8_t*>(&propName[0]), propName.length()), runtime.createValue(value));
        }

        static void Enumerator(const v8::PropertyCallbackInfo<v8::Array>& info)
        {
          v8::Local<v8::External> data = v8::Local<v8::External>::Cast(info.Data());
          HostObjectProxy* hostObjectProxy = reinterpret_cast<HostObjectProxy*>(data->Value());

          V8Runtime& runtime = hostObjectProxy->runtime_;
          std::shared_ptr<jsi::HostObject> hostObject = hostObjectProxy->hostObject_;

          std::vector<jsi::PropNameID> propIds = hostObject->getPropertyNames(runtime);

          v8::Local<v8::Array> result = v8::Array::New(info.GetIsolate(), propIds.size());
          v8::Local<v8::Context> context = info.GetIsolate()->GetCurrentContext();

          for (int i = 0; i < result->Length(); i++)
          {
            v8::Local<v8::Value> propIdValue = runtime.valueRef(propIds[i]);
            if (!result->Set(context, i, propIdValue).FromJust()) { std::terminate(); };
          }

          info.GetReturnValue().Set(result);
        }

        HostObjectProxy(V8Runtime& rt, const std::shared_ptr<jsi::HostObject>& hostObject) : runtime_(rt), hostObject_(hostObject) {}
        std::shared_ptr<jsi::HostObject> getHostObject() { return hostObject_; }
      private:
        friend class HostObjectLifetimeTracker;
        void destroy() override {
          hostObject_.reset();
        }

        V8Runtime& runtime_;
        std::shared_ptr<jsi::HostObject> hostObject_;
      };

      class HostFunctionProxy : public IHostProxy {
      public:
        static void call(HostFunctionProxy& hostFunctionProxy, const v8::FunctionCallbackInfo<v8::Value>& callbackInfo) {
          V8Runtime& runtime = const_cast<V8Runtime&>(hostFunctionProxy.runtime_);
          v8::Isolate* isolate = callbackInfo.GetIsolate();

          std::vector<jsi::Value> argsVector;
          for (int i = 0; i < callbackInfo.Length(); i++)
          {
            argsVector.push_back(hostFunctionProxy.runtime_.createValue(callbackInfo[i]));
          }

          const jsi::Value& thisVal = runtime.createValue(callbackInfo.This());

          jsi::Value result;
          try {
            result = hostFunctionProxy.func_(runtime, thisVal, argsVector.data(), callbackInfo.Length());
          }
          catch (const jsi::JSError& error) {
            callbackInfo.GetReturnValue().Set(v8::Undefined(isolate));

            // Schedule to throw the exception back to JS.
            isolate->ThrowException(runtime.valueRef(error.value()));
            return;
          }
          catch (const std::exception& ex) {
            callbackInfo.GetReturnValue().Set(v8::Undefined(isolate));

            // Schedule to throw the exception back to JS.
            v8::Local<v8::String> message = v8::String::NewFromUtf8(isolate, ex.what(), v8::NewStringType::kNormal).ToLocalChecked();
            isolate->ThrowException(v8::Exception::Error(message));
            return;
          }
          catch (...) {
            callbackInfo.GetReturnValue().Set(v8::Undefined(isolate));

            // Schedule to throw the exception back to JS.
            v8::Local<v8::String> message = v8::String::NewFromOneByte(isolate, reinterpret_cast<const uint8_t*>("<Unknown exception in host function callback>"), v8::NewStringType::kNormal).ToLocalChecked();
            isolate->ThrowException(v8::Exception::Error(message));
            return;
          }

          callbackInfo.GetReturnValue().Set(runtime.valueRef(result));
        }

      public:
        static void HostFunctionCallback(const v8::FunctionCallbackInfo<v8::Value>& info)
        {
          v8::HandleScope handle_scope(v8::Isolate::GetCurrent());
          v8::Local<v8::External> data = v8::Local<v8::External>::Cast(info.Data());
          HostFunctionProxy* hostFunctionProxy = reinterpret_cast<HostFunctionProxy*> (data->Value());
          hostFunctionProxy->call(*hostFunctionProxy, info);
        }

        HostFunctionProxy(facebook::v8runtime::V8Runtime& runtime, jsi::HostFunctionType func)
          : func_(std::move(func)), runtime_(runtime) {};

      private:
        friend class HostObjectLifetimeTracker;
        void destroy() override {
          func_ = [](Runtime& rt, const jsi::Value& thisVal, const jsi::Value* args, size_t count) {return jsi::Value::undefined(); };
        }

        jsi::HostFunctionType func_;
        facebook::v8runtime::V8Runtime& runtime_;
      };

      class V8StringValue final : public PointerValue {
        V8StringValue(v8::Local<v8::String> str);
        ~V8StringValue();

        void invalidate() override;

        v8::Persistent<v8::String> v8String_;
      protected:
        friend class V8Runtime;
      };

      class V8ObjectValue final : public PointerValue {
        V8ObjectValue(v8::Local<v8::Object> obj);

        ~V8ObjectValue();

        void invalidate() override;

        v8::Persistent<v8::Object> v8Object_;

      protected:
        friend class V8Runtime;
      };

      PointerValue* cloneString(const Runtime::PointerValue* pv) override;
      PointerValue* cloneObject(const Runtime::PointerValue* pv) override;
      PointerValue* clonePropNameID(const Runtime::PointerValue* pv) override;

      jsi::PropNameID createPropNameIDFromAscii(const char* str, size_t length)
        override;
      jsi::PropNameID createPropNameIDFromUtf8(const uint8_t* utf8, size_t length)
        override;
      jsi::PropNameID createPropNameIDFromString(const jsi::String& str) override;
      std::string utf8(const jsi::PropNameID&) override;
      bool compare(const jsi::PropNameID&, const jsi::PropNameID&) override;

      jsi::String createStringFromAscii(const char* str, size_t length) override;
      jsi::String createStringFromUtf8(const uint8_t* utf8, size_t length) override;
      std::string utf8(const jsi::String&) override;

      jsi::Object createObject() override;
      jsi::Object createObject(std::shared_ptr<jsi::HostObject> ho) override;
      virtual std::shared_ptr<jsi::HostObject> getHostObject(
        const jsi::Object&) override;
      jsi::HostFunctionType& getHostFunction(const jsi::Function&) override;

      jsi::Value getProperty(const jsi::Object&, const jsi::String& name) override;
      jsi::Value getProperty(const jsi::Object&, const jsi::PropNameID& name)
        override;
      bool hasProperty(const jsi::Object&, const jsi::String& name) override;
      bool hasProperty(const jsi::Object&, const jsi::PropNameID& name) override;
      void setPropertyValue(
        jsi::Object&,
        const jsi::String& name,
        const jsi::Value& value) override;
      void setPropertyValue(
        jsi::Object&,
        const jsi::PropNameID& name,
        const jsi::Value& value) override;
      bool isArray(const jsi::Object&) const override;
      bool isArrayBuffer(const jsi::Object&) const override;
      bool isFunction(const jsi::Object&) const override;
	  bool isPromise(const jsi::Object&) const override;
      bool isHostObject(const jsi::Object&) const override;
      bool isHostFunction(const jsi::Function&) const override;
      jsi::Array getPropertyNames(const jsi::Object&) override;

      jsi::WeakObject createWeakObject(const jsi::Object&) override;
      jsi::Value lockWeakObject(const jsi::WeakObject&) override;

      jsi::Array createArray(size_t length) override;
      size_t size(const jsi::Array&) override;
      size_t size(const jsi::ArrayBuffer&) override;
      uint8_t* data(const jsi::ArrayBuffer&) override;
      jsi::Value getValueAtIndex(const jsi::Array&, size_t i) override;
      void setValueAtIndexImpl(jsi::Array&, size_t i, const jsi::Value& value)
        override;

      jsi::Function createFunctionFromHostFunction(
        const jsi::PropNameID& name,
        unsigned int paramCount,
        jsi::HostFunctionType func) override;

      jsi::Value call(
        const jsi::Function&,
        const jsi::Value& jsThis,
        const jsi::Value* args,
        size_t count) override;
      jsi::Value callAsConstructor(
        const jsi::Function&,
        const jsi::Value* args,
        size_t count) override;

      jsi::PromiseResolver createPromiseResolver() override;
      void Resolve(jsi::PromiseResolver& resolver, jsi::Value& value) override;
      void Reject(jsi::PromiseResolver& resolver, jsi::Value& value) override;
      jsi::Promise getPromise(jsi::PromiseResolver& resolver) override;

	    jsi::Promise Catch(jsi::Promise& promise, jsi::Function& func) override;
	    jsi::Promise Then(jsi::Promise& promise, jsi::Function& func) override;

      virtual jsi::Value Result(jsi::Promise& promise) override;
      virtual bool isPending(jsi::Promise& promise) override;
      virtual bool isFulfilled(jsi::Promise& promise) override;
      virtual bool isRejected(jsi::Promise& promise) override;

      bool strictEquals(const jsi::String& a, const jsi::String& b) const override;
      bool strictEquals(const jsi::Object& a, const jsi::Object& b) const override;
      bool instanceOf(const jsi::Object& o, const jsi::Function& f) override;

      void AddHostObjectLifetimeTracker(std::shared_ptr<HostObjectLifetimeTracker> hostObjectLifetimeTracker);

    private:

      v8::Local<v8::Context> CreateContext(v8::Isolate* isolate);
      bool ExecuteString(v8::Local<v8::String> source, v8::Local<v8::Value> name, bool print_result, bool report_exceptions);
      void ReportException(v8::TryCatch* try_catch);

      v8::Isolate* GetIsolate() const { return isolate_; }

      // Basically convenience casts
      static v8::Local<v8::String> stringRef(const jsi::String& str);
      static v8::Local<v8::Value> valueRef(const jsi::PropNameID& sym);
      static v8::Local<v8::Object> objectRef(const jsi::Object& obj);

      v8::Local<v8::Value> valueRef(const jsi::Value& value);
      jsi::Value createValue(v8::Local<v8::Value> value) const;

      // Factory methods for creating String/Object
      jsi::String createString(v8::Local<v8::String> stringRef) const;
      jsi::PropNameID createPropNameID(v8::Local<v8::Value> propValRef);
      jsi::Object createObject(v8::Local<v8::Object> objectRef) const;

      // Used by factory methods and clone methods
      jsi::Runtime::PointerValue* makeStringValue(v8::Local<v8::String> str) const;
      jsi::Runtime::PointerValue* makeObjectValue(v8::Local<v8::Object> obj) const;

      std::unique_ptr<v8::Platform> platform_;
      v8::Isolate* isolate_;
      v8::Global<v8::Context> context_;
      v8::Isolate::CreateParams create_params_;

      v8::Persistent<v8::FunctionTemplate> hostFunctionTemplate_;
      v8::Persistent<v8::Function> hostObjectConstructor_;

      std::list<std::shared_ptr<HostObjectLifetimeTracker>> hostObjectLifetimeTrackerList_;

      std::string desc_;
    };

    // String utilities
    namespace {
      std::string JSStringToSTLString(v8::Isolate* isolate, v8::Local<v8::String> string) {
        int utfLen = string->Utf8Length(isolate);
        std::string result;
        result.resize(utfLen);
        string->WriteUtf8(isolate, &result[0], utfLen);
        return result;
      }

      // Extracts a C string from a V8 Utf8Value.
      const char* ToCString(const v8::String::Utf8Value& value) {
        return *value ? *value : "<string conversion failed>";
      }
    } // namespace

    void V8Runtime::AddHostObjectLifetimeTracker(std::shared_ptr<HostObjectLifetimeTracker> hostObjectLifetimeTracker) {
      // Note that we are letting the list grow in definitely as of now.. The list gets cleaned up when the runtime is teared down.
      // TODO :: We should remove entries from the list as the objects are garbage collected.
      hostObjectLifetimeTrackerList_.push_back(hostObjectLifetimeTracker);
    }

    V8Runtime::V8Runtime() {
      // NewDefaultPlatform is causing linking error on droid.
      // The issue is similar to what is mentioned here https://groups.google.com/forum/#!topic/v8-users/Jb1VSouy2Z0
      // We are trying to figure out solution but using it's deprecated cousin CreateDefaultPlatform for now.

      // platform_ = v8::platform::NewDefaultPlatform();
      // v8::V8::InitializePlatform(platform_.get());

      int argc = 2;
      const char* argv[] = { "", "--expose_gc" };

      v8::V8::SetFlagsFromCommandLine(&argc, const_cast<char **>(argv), false);
      v8::V8::InitializeExternalStartupData("d:\\work\\jsiapp\\x64\\Debug\\");

      v8::Platform *platform = v8::platform::CreateDefaultPlatform();
      v8::V8::InitializePlatform(platform);

      v8::V8::Initialize();

      create_params_.array_buffer_allocator = v8::ArrayBuffer::Allocator::NewDefaultAllocator();

      isolate_ = v8::Isolate::New(create_params_);
      isolate_->Enter();

      v8::HandleScope handleScope(isolate_);
      context_.Reset(GetIsolate(), CreateContext(isolate_));

      v8::Context::Scope context_scope(context_.Get(GetIsolate()));

      // Create and keep the constuctor for creating Host objects.
      v8::Local<v8::FunctionTemplate> constructorForHostObjectTemplate = v8::FunctionTemplate::New(isolate_);
      v8::Local<v8::ObjectTemplate> hostObjectTemplate = constructorForHostObjectTemplate->InstanceTemplate();
      hostObjectTemplate->SetHandler(v8::NamedPropertyHandlerConfiguration(HostObjectProxy::Get, HostObjectProxy::Set, nullptr, nullptr, HostObjectProxy::Enumerator));
      hostObjectTemplate->SetInternalFieldCount(1);
      hostObjectConstructor_.Reset(isolate_, constructorForHostObjectTemplate->GetFunction());
    }

    V8Runtime::~V8Runtime() {
      hostObjectConstructor_.Reset();
      context_.Reset();

      for (std::shared_ptr<HostObjectLifetimeTracker> hostObjectLifetimeTracker : hostObjectLifetimeTrackerList_) {
        hostObjectLifetimeTracker->ResetHostObject(false /*isGC*/);
      }

      isolate_->Exit();
      isolate_->Dispose();

      v8::V8::Dispose();
      v8::V8::ShutdownPlatform();
      delete create_params_.array_buffer_allocator;
    }

    void V8Runtime::evaluateJavaScript(
      std::unique_ptr<const jsi::Buffer> buffer,
      const std::string& sourceURL) {

      _ISOLATE_CONTEXT_ENTER

        int size = buffer->size();
      const char* str = reinterpret_cast<const char*>(buffer->data());

      v8::MaybeLocal<v8::String> urlV8String = v8::String::NewFromUtf8(isolate, reinterpret_cast<const char*>(sourceURL.c_str()));
      v8::MaybeLocal<v8::String> sourceV8String = v8::String::NewFromUtf8(isolate, str, v8::NewStringType::kNormal, size);

      // Note : We crash if either is empty.
      ExecuteString(sourceV8String.ToLocalChecked(), urlV8String.ToLocalChecked(), true, true);
    }

    // The callback that is invoked by v8 whenever the JavaScript 'print'
  // function is called.  Prints its arguments on stdout separated by
  // spaces and ending with a newline.
    void Print(const v8::FunctionCallbackInfo<v8::Value>& args) {
      bool first = true;
      for (int i = 0; i < args.Length(); i++) {
        v8::HandleScope handle_scope(args.GetIsolate());
        if (first) {
          first = false;
        }
        else {
          printf(" ");
        }
        v8::String::Utf8Value str(args.GetIsolate(), args[i]);
        const char* cstr = ToCString(str);
        printf("%s", cstr);
      }
      printf("\n");
      fflush(stdout);
    }

    v8::Local<v8::Context> V8Runtime::CreateContext(v8::Isolate* isolate) {
      // Create a template for the global object.
      v8::Local<v8::ObjectTemplate> global = v8::ObjectTemplate::New(isolate);

      // Bind the global 'print' function to the C++ Print callback. Useful for debugging.
      global->Set(
        v8::String::NewFromUtf8(isolate, "print", v8::NewStringType::kNormal)
        .ToLocalChecked(),
        v8::FunctionTemplate::New(isolate, Print));

      v8::Local<v8::Context> context = v8::Context::New(isolate, NULL, global);
      context->SetAlignedPointerInEmbedderData(1, this);
      return context;
    }

    bool V8Runtime::ExecuteString(v8::Local<v8::String> source, v8::Local<v8::Value> name, bool print_result, bool report_exceptions) {
      _ISOLATE_CONTEXT_ENTER
        v8::TryCatch try_catch(isolate);
      v8::ScriptOrigin origin(name);
      v8::Local<v8::Context> context(isolate->GetCurrentContext());
      v8::Local<v8::Script> script;
      if (!v8::Script::Compile(context, source, &origin).ToLocal(&script)) {
        // Print errors that happened during compilation.
        if (report_exceptions)
          ReportException(&try_catch);
        return false;
      }
      else {
        v8::Local<v8::Value> result;
        if (!script->Run(context).ToLocal(&result)) {
          assert(try_catch.HasCaught());
          // Print errors that happened during execution.
          if (report_exceptions) {
            ReportException(&try_catch);
          }
          return false;
        }
        else {
          assert(!try_catch.HasCaught());
          if (print_result && !result->IsUndefined()) {
            // If all went well and the result wasn't undefined then print
            // the returned value.
            v8::String::Utf8Value str(isolate, result);
            const char* cstr = ToCString(str);
            printf("%s\n", cstr);
          }
          return true;
        }
      }
    }

    void V8Runtime::ReportException(v8::TryCatch* try_catch) {
      _ISOLATE_CONTEXT_ENTER
        v8::String::Utf8Value exception(isolate, try_catch->Exception());
      const char* exception_string = ToCString(exception);
      v8::Local<v8::Message> message = try_catch->Message();
      if (message.IsEmpty()) {
        // V8 didn't provide any extra information about this error; just
        // throw the exception.
        throw jsi::JSError(*this, "<Unknown exception>");
      }
      else {
        // Print (filename):(line number): (message).

        std::stringstream sstr;

        v8::String::Utf8Value filename(isolate, message->GetScriptOrigin().ResourceName());
        v8::Local<v8::Context> context(isolate->GetCurrentContext());
        const char* filename_string = ToCString(filename);
        int linenum = message->GetLineNumber(context).FromJust();
        sstr << filename_string << ":" << linenum << ": " << exception_string << std::endl;

        // Print line of source code.
        v8::String::Utf8Value sourceline(isolate, message->GetSourceLine(context).ToLocalChecked());
        const char* sourceline_string = ToCString(sourceline);
        sstr << sourceline_string << std::endl;

        // Print wavy underline (GetUnderline is deprecated).
        int start = message->GetStartColumn(context).FromJust();
        for (int i = 0; i < start; i++) {
          sstr << " ";
        }
        int end = message->GetEndColumn(context).FromJust();
        for (int i = start; i < end; i++) {
          sstr << "^";
        }
        sstr << std::endl;

        v8::Local<v8::Value> stack_trace_string;
        if (try_catch->StackTrace(context).ToLocal(&stack_trace_string) && stack_trace_string->IsString() && v8::Local<v8::String>::Cast(stack_trace_string)->Length() > 0) {
          v8::String::Utf8Value stack_trace(isolate, stack_trace_string);
          const char* stack_trace_string = ToCString(stack_trace);
          sstr << stack_trace_string << std::endl;
        }

        throw jsi::JSError(*this, sstr.str());
      }
    }

    jsi::Object V8Runtime::global() {
      _ISOLATE_CONTEXT_ENTER
        return createObject(context_.Get(isolate)->Global());
    }

    std::string V8Runtime::description() {
      if (desc_.empty()) {
        desc_ = std::string("<V8Runtime>");
      }
      return desc_;
    }

    bool V8Runtime::isInspectable() {
      return false;
    }

    V8Runtime::V8StringValue::V8StringValue(v8::Local<v8::String> str)
      : v8String_(v8::Isolate::GetCurrent(), str)
    {
    }

    void V8Runtime::V8StringValue::invalidate() {
      delete this;
    }

    V8Runtime::V8StringValue::~V8StringValue() {
      v8String_.Reset();
    }

    V8Runtime::V8ObjectValue::V8ObjectValue(v8::Local<v8::Object> obj)
      : v8Object_(v8::Isolate::GetCurrent(), obj) {}

    void V8Runtime::V8ObjectValue::invalidate() {
      delete this;
    }

    V8Runtime::V8ObjectValue::~V8ObjectValue() {
      v8Object_.Reset();
    }

    // Shallow clone
    jsi::Runtime::PointerValue* V8Runtime::cloneString(const jsi::Runtime::PointerValue* pv) {
      if (!pv) {
        return nullptr;
      }

      _ISOLATE_CONTEXT_ENTER
        const V8StringValue* string = static_cast<const V8StringValue*>(pv);
      return makeStringValue(string->v8String_.Get(GetIsolate()));
    }

    jsi::Runtime::PointerValue* V8Runtime::cloneObject(const jsi::Runtime::PointerValue* pv) {
      if (!pv) {
        return nullptr;
      }

      _ISOLATE_CONTEXT_ENTER
        const V8ObjectValue* object = static_cast<const V8ObjectValue*>(pv);
      return makeObjectValue(object->v8Object_.Get(GetIsolate()));
    }

    jsi::Runtime::PointerValue* V8Runtime::clonePropNameID(const jsi::Runtime::PointerValue* pv) {
      if (!pv) {
        return nullptr;
      }

      _ISOLATE_CONTEXT_ENTER
        const V8StringValue* string = static_cast<const V8StringValue*>(pv);
      return makeStringValue(string->v8String_.Get(GetIsolate()));
    }

    jsi::PropNameID V8Runtime::createPropNameIDFromAscii(const char* str, size_t length) {
      _ISOLATE_CONTEXT_ENTER
        v8::Local<v8::String> v8String;
      if (!v8::String::NewFromOneByte(GetIsolate(), reinterpret_cast<uint8_t*>(const_cast<char*>(str)), v8::NewStringType::kNormal, length).ToLocal(&v8String)) {
        std::stringstream strstream;
        strstream << "Unable to create property id: " << str;
        throw jsi::JSError(*this, strstream.str());
      }

      auto res = createPropNameID(v8String);
      return res;
    }

    jsi::PropNameID V8Runtime::createPropNameIDFromUtf8(const uint8_t* utf8, size_t length) {
      _ISOLATE_CONTEXT_ENTER
        v8::Local<v8::String> v8String;
      if (!v8::String::NewFromUtf8(GetIsolate(), reinterpret_cast<const char*>(utf8), v8::NewStringType::kNormal, length).ToLocal(&v8String)) {
        std::stringstream strstream;
        strstream << "Unable to create property id: " << utf8;
        throw jsi::JSError(*this, strstream.str());
      }

      auto res = createPropNameID(v8String);
      return res;
    }

    jsi::PropNameID V8Runtime::createPropNameIDFromString(const jsi::String& str) {
      _ISOLATE_CONTEXT_ENTER
        return createPropNameID(stringRef(str));
    }

    std::string V8Runtime::utf8(const jsi::PropNameID& sym) {
      _ISOLATE_CONTEXT_ENTER
        return JSStringToSTLString(GetIsolate(), v8::Local<v8::String>::Cast(valueRef(sym)));
    }

    bool V8Runtime::compare(const jsi::PropNameID& a, const jsi::PropNameID& b) {
      _ISOLATE_CONTEXT_ENTER
        return valueRef(a)->Equals(GetIsolate()->GetCurrentContext(), valueRef(b)).ToChecked();
    }

    jsi::String V8Runtime::createStringFromAscii(const char* str, size_t length) {
      return this->createStringFromUtf8(reinterpret_cast<const uint8_t*>(str), length);
    }

    jsi::String V8Runtime::createStringFromUtf8(const uint8_t* str, size_t length) {
      _ISOLATE_CONTEXT_ENTER
        v8::Local<v8::String> v8string;
      if (!v8::String::NewFromUtf8(v8::Isolate::GetCurrent(), reinterpret_cast<const char*>(str), v8::NewStringType::kNormal, length).ToLocal(&v8string)) {
        throw jsi::JSError(*this, "V8 string creation failed.");
      }

      jsi::String jsistr = createString(v8string);
      return jsistr;
    }

    std::string V8Runtime::utf8(const jsi::String& str) {
      _ISOLATE_CONTEXT_ENTER
        return JSStringToSTLString(GetIsolate(), stringRef(str));
    }

    jsi::Object V8Runtime::createObject() {
      _ISOLATE_CONTEXT_ENTER
        return createObject(v8::Object::New(GetIsolate()));
    }

    jsi::Object V8Runtime::createObject(std::shared_ptr<jsi::HostObject> hostobject) {
      _ISOLATE_CONTEXT_ENTER
        HostObjectProxy* hostObjectProxy = new HostObjectProxy(*this, hostobject);
      v8::Local<v8::Object> newObject;
      if (!hostObjectConstructor_.Get(isolate_)->NewInstance(isolate_->GetCurrentContext()).ToLocal(&newObject)) {
        throw jsi::JSError(*this, "HostObject construction failed!!");
      }

      newObject->SetInternalField(0, v8::Local<v8::External>::New(GetIsolate(), v8::External::New(GetIsolate(), hostObjectProxy)));

      AddHostObjectLifetimeTracker(std::make_shared<HostObjectLifetimeTracker>(*this, newObject, hostObjectProxy));

      return createObject(newObject);
    }

    std::shared_ptr<jsi::HostObject> V8Runtime::getHostObject(const jsi::Object& obj) {
      _ISOLATE_CONTEXT_ENTER
        v8::Local<v8::External> internalField = v8::Local<v8::External>::Cast(objectRef(obj)->GetInternalField(0));
      HostObjectProxy* hostObjectProxy = reinterpret_cast<HostObjectProxy*>(internalField->Value());
      return hostObjectProxy->getHostObject();
    }

    jsi::Value V8Runtime::getProperty(const jsi::Object& obj, const jsi::String& name) {
      _ISOLATE_CONTEXT_ENTER
        return createValue(objectRef(obj)->Get(stringRef(name)));

      v8::MaybeLocal<v8::Value> result = objectRef(obj)->Get(isolate_->GetCurrentContext(), stringRef(name));
      if (result.IsEmpty()) throw jsi::JSError(*this, "V8Runtime::getProperty failed.");
      return createValue(result.ToLocalChecked());
    }

    jsi::Value V8Runtime::getProperty(const jsi::Object& obj, const jsi::PropNameID& name) {
      _ISOLATE_CONTEXT_ENTER
        v8::MaybeLocal<v8::Value> result = objectRef(obj)->Get(isolate_->GetCurrentContext(), valueRef(name));
      if (result.IsEmpty()) throw jsi::JSError(*this, "V8Runtime::getProperty failed.");
      return createValue(result.ToLocalChecked());
    }

    bool V8Runtime::hasProperty(const jsi::Object& obj, const jsi::String& name) {
      _ISOLATE_CONTEXT_ENTER
        v8::Maybe<bool> result = objectRef(obj)->Has(isolate_->GetCurrentContext(), stringRef(name));
      if (result.IsNothing()) throw jsi::JSError(*this, "V8Runtime::setPropertyValue failed.");
      return result.FromJust();
    }

    bool V8Runtime::hasProperty(const jsi::Object& obj, const jsi::PropNameID& name) {
      _ISOLATE_CONTEXT_ENTER
        v8::Maybe<bool> result = objectRef(obj)->Has(isolate_->GetCurrentContext(), valueRef(name));
      if (result.IsNothing()) throw jsi::JSError(*this, "V8Runtime::setPropertyValue failed.");
      return result.FromJust();
    }

    void V8Runtime::setPropertyValue(jsi::Object& object, const jsi::PropNameID& name, const jsi::Value& value) {
      _ISOLATE_CONTEXT_ENTER
        v8::Maybe<bool> result = objectRef(object)->Set(isolate_->GetCurrentContext(), valueRef(name), valueRef(value));
      if (!result.FromMaybe(false)) throw jsi::JSError(*this, "V8Runtime::setPropertyValue failed.");
    }

    void V8Runtime::setPropertyValue(jsi::Object& object, const jsi::String& name, const jsi::Value& value) {
      _ISOLATE_CONTEXT_ENTER
        v8::Maybe<bool> result = objectRef(object)->Set(isolate_->GetCurrentContext(), stringRef(name), valueRef(value));
      if (!result.FromMaybe(false)) throw jsi::JSError(*this, "V8Runtime::setPropertyValue failed.");
    }

    bool V8Runtime::isArray(const jsi::Object& obj) const {
      _ISOLATE_CONTEXT_ENTER
        return objectRef(obj)->IsArray();
    }

    bool V8Runtime::isArrayBuffer(const jsi::Object& /*obj*/) const {
      throw std::runtime_error("Unsupported");
    }

    uint8_t* V8Runtime::data(const jsi::ArrayBuffer& obj) {
      throw std::runtime_error("Unsupported");
    }

    size_t V8Runtime::size(const jsi::ArrayBuffer& /*obj*/) {
      throw std::runtime_error("Unsupported");
    }

    bool V8Runtime::isFunction(const jsi::Object& obj) const {
      _ISOLATE_CONTEXT_ENTER
        return objectRef(obj)->IsFunction();
    }

	bool V8Runtime::isPromise(const jsi::Object& obj) const {
		_ISOLATE_CONTEXT_ENTER
			return objectRef(obj)->IsPromise();
	}

    bool V8Runtime::isHostObject(const jsi::Object& obj) const {
      _ISOLATE_CONTEXT_ENTER
        std::abort();
    }

    // Very expensive
    jsi::Array V8Runtime::getPropertyNames(const jsi::Object& obj) {
      _ISOLATE_CONTEXT_ENTER
        v8::Local<v8::Array> propNames = objectRef(obj)->GetPropertyNames();
      return createObject(propNames).getArray(*this);
    }

    jsi::WeakObject V8Runtime::createWeakObject(const jsi::Object&) {
      throw std::logic_error("Not implemented");
    }

    jsi::Value V8Runtime::lockWeakObject(const jsi::WeakObject&) {
      throw std::logic_error("Not implemented");
    }

    jsi::Array V8Runtime::createArray(size_t length) {
      _ISOLATE_CONTEXT_ENTER
        return createObject(v8::Array::New(GetIsolate(), length)).getArray(*this);
    }

    size_t V8Runtime::size(const jsi::Array& arr) {
      _ISOLATE_CONTEXT_ENTER
        v8::Local<v8::Array> array = v8::Local<v8::Array>::Cast(objectRef(arr));
      return array->Length();
    }

    jsi::Value V8Runtime::getValueAtIndex(const jsi::Array& arr, size_t i) {
      _ISOLATE_CONTEXT_ENTER
        v8::Local<v8::Array> array = v8::Local<v8::Array>::Cast(objectRef(arr));
      return createValue(array->Get(i));
    }

    void V8Runtime::setValueAtIndexImpl(jsi::Array& arr, size_t i, const jsi::Value& value) {
      _ISOLATE_CONTEXT_ENTER
        v8::Local<v8::Array> array = v8::Local<v8::Array>::Cast(objectRef(arr));
      array->Set(i, valueRef(value));
    }

    jsi::Function V8Runtime::createFunctionFromHostFunction(const jsi::PropNameID& name, unsigned int paramCount, jsi::HostFunctionType func) {
      _ISOLATE_CONTEXT_ENTER

      HostFunctionProxy* hostFunctionProxy = new HostFunctionProxy(*this, func);

      v8::Local<v8::Function> newFunction;
      if (!v8::Function::New(isolate_->GetCurrentContext(), HostFunctionProxy::HostFunctionCallback,
        v8::Local<v8::External>::New(GetIsolate(), v8::External::New(GetIsolate(), hostFunctionProxy))).ToLocal(&newFunction)) {
        throw jsi::JSError(*this, "Creation of HostFunction failed.");
      }

      AddHostObjectLifetimeTracker(std::make_shared<HostObjectLifetimeTracker>(*this, newFunction, hostFunctionProxy));

      return createObject(newFunction).getFunction(*this);
    }

    bool V8Runtime::isHostFunction(const jsi::Function& obj) const {
      std::abort();
      return false;
    }

    jsi::HostFunctionType& V8Runtime::getHostFunction(const jsi::Function& obj) {
      std::abort();
    }

    jsi::Value V8Runtime::call(const jsi::Function& jsiFunc, const jsi::Value& jsThis, const jsi::Value* args, size_t count) {
      _ISOLATE_CONTEXT_ENTER
        v8::Local<v8::Function> func = v8::Local<v8::Function>::Cast(objectRef(jsiFunc));
      std::vector<v8::Local<v8::Value>> argv;
      for (int i = 0; i < count; i++)
      {
        argv.push_back(valueRef(args[i]));
      }

      v8::MaybeLocal<v8::Value> result = func->Call(isolate_->GetCurrentContext(), valueRef(jsThis), count, argv.data());

      // Call can return 
      if (result.IsEmpty()) {
        return createValue(v8::Undefined(GetIsolate()));
      }
      else {
        return createValue(result.ToLocalChecked());
      }
    }

    jsi::Value V8Runtime::callAsConstructor(const jsi::Function& jsiFunc, const jsi::Value* args, size_t count) {
      _ISOLATE_CONTEXT_ENTER
        v8::Local<v8::Function> func = v8::Local<v8::Function>::Cast(objectRef(jsiFunc));
      std::vector<v8::Local<v8::Value>> argv;
      for (int i = 0; i < count; i++)
      {
        argv.push_back(valueRef(args[i]));
      }

      v8::Local<v8::Object> newObject;
      if (!func->NewInstance(GetIsolate()->GetCurrentContext(), count, argv.data()).ToLocal(&newObject)) {
        new jsi::JSError(*this, "Object construction failed!!");
      }

      return createValue(newObject);
    }

    jsi::PromiseResolver V8Runtime::createPromiseResolver() {
      _ISOLATE_CONTEXT_ENTER
      return createObject(v8::Promise::Resolver::New(isolate_)).getPromiseResolver(*this);
    }

    void V8Runtime::Resolve(jsi::PromiseResolver& resolver, jsi::Value& value) {
      _ISOLATE_CONTEXT_ENTER
      v8::Local<v8::Promise::Resolver> v8promiseResolver = v8::Local<v8::Promise::Resolver>::Cast(objectRef(resolver));
      v8promiseResolver->Resolve(valueRef(value));
    }

    void V8Runtime::Reject(jsi::PromiseResolver& resolver, jsi::Value& value) {
      _ISOLATE_CONTEXT_ENTER
      v8::Local<v8::Promise::Resolver> v8promiseResolver = v8::Local<v8::Promise::Resolver>::Cast(objectRef(resolver));
      v8promiseResolver->Reject(valueRef(value));
    }

    jsi::Promise V8Runtime::getPromise(jsi::PromiseResolver& resolver) {
      _ISOLATE_CONTEXT_ENTER
      v8::Local<v8::Promise::Resolver> v8promiseResolver = v8::Local<v8::Promise::Resolver>::Cast(objectRef(resolver));
      return createObject(v8promiseResolver->GetPromise()).getPromise(*this);
    }

	jsi::Promise V8Runtime::Catch(jsi::Promise& promise, jsi::Function& func) {
		_ISOLATE_CONTEXT_ENTER
      if (promise.isPending(*this)) {
        v8::Local<v8::Promise> v8promise = v8::Local<v8::Promise>::Cast(objectRef(promise));
        v8::Local<v8::Function> v8Function = v8::Local<v8::Function>::Cast(objectRef(func));

        v8::Local<v8::Promise> newPromise;
        if (!v8promise->Catch(isolate->GetCurrentContext(), v8Function).ToLocal(&newPromise)) {
          throw jsi::JSError(*this, "Promise::Then failed.");
        }

        return createObject(newPromise).getPromise(*this);
      }
      else if (promise.isRejected(*this)) {
        std::vector<jsi::Value> vargs;
        vargs.push_back(std::move(promise.Result(*this)));
        const jsi::Value* args = vargs.data();
        jsi::Value ret = func.call(*this, args, vargs.size());
        if (ret.isObject() && ret.asObject(*this).isPromise(*this)) {
          return ret.asObject(*this).getPromise(*this);
        }
        else {
          jsi::PromiseResolver resolver = jsi::PromiseResolver::create(*this);
          resolver.Resolve(*this, ret);
          return resolver.getPromise(*this);
        }
      }
      else {
        // TODO :: Is this the right behaviour ?
        jsi::PromiseResolver resolver = jsi::PromiseResolver::create(*this);
        return resolver.getPromise(*this);
      }
	}

	jsi::Promise V8Runtime::Then(jsi::Promise& promise, jsi::Function& func) {
		_ISOLATE_CONTEXT_ENTER
    
    if(promise.isPending(*this)) {
      v8::Local<v8::Promise> v8promise = v8::Local<v8::Promise>::Cast(objectRef(promise));
      v8::Local<v8::Function> v8Function = v8::Local<v8::Function>::Cast(objectRef(func));
      
      v8::Local<v8::Promise> newPromise;
      if (!v8promise->Then(isolate->GetCurrentContext(), v8Function).ToLocal(&newPromise)) {
        throw jsi::JSError(*this, "Promise::Then failed.");
      }

      return createObject(newPromise).getPromise(*this);
    } else if (promise.isFulfilled(*this)) {
      std::vector<jsi::Value> vargs;
      vargs.push_back(std::move(promise.Result(*this)));
      const jsi::Value* args = vargs.data();
      jsi::Value ret = func.call(*this, args, vargs.size());
      if (ret.isObject() && ret.asObject(*this).isPromise(*this)) {
        return ret.asObject(*this).getPromise(*this);
      }
      else {
        jsi::PromiseResolver resolver = jsi::PromiseResolver::create(*this);
        resolver.Resolve(*this, ret);
        return resolver.getPromise(*this);
      }
    }
    else {
      // TODO :: Is this the right behaviour ?
      jsi::PromiseResolver resolver = jsi::PromiseResolver::create(*this);
      return resolver.getPromise(*this);
    }
  }
  
  jsi::Value V8Runtime::Result(jsi::Promise& promise) {
    _ISOLATE_CONTEXT_ENTER
    v8::Local<v8::Promise> v8Promise =  v8::Local<v8::Promise>::Cast(objectRef(promise));
    v8::Local<v8::Value> v8Value = v8Promise->Result();
    return createValue(v8Value);
  }
  
  bool V8Runtime::isPending(jsi::Promise& promise) {
    _ISOLATE_CONTEXT_ENTER
    v8::Local<v8::Promise> v8Promise = v8::Local<v8::Promise>::Cast(objectRef(promise));
    return v8Promise->State() == v8::Promise::kPending;
  }

  bool V8Runtime::isFulfilled(jsi::Promise& promise) {
    _ISOLATE_CONTEXT_ENTER
    v8::Local<v8::Promise> v8Promise = v8::Local<v8::Promise>::Cast(objectRef(promise));
    return v8Promise->State() == v8::Promise::kFulfilled;
  }

  bool V8Runtime::isRejected(jsi::Promise& promise) {
    _ISOLATE_CONTEXT_ENTER
    v8::Local<v8::Promise> v8Promise = v8::Local<v8::Promise>::Cast(objectRef(promise));
    return v8Promise->State() == v8::Promise::kRejected;
  }

    bool V8Runtime::strictEquals(const jsi::String& a, const jsi::String& b) const {
      _ISOLATE_CONTEXT_ENTER
        return stringRef(a)->StrictEquals(stringRef(b));
    }

    bool V8Runtime::strictEquals(const jsi::Object& a, const jsi::Object& b) const {
      _ISOLATE_CONTEXT_ENTER
        return objectRef(a)->StrictEquals(objectRef(b));
    }

    bool V8Runtime::instanceOf(const jsi::Object& o, const jsi::Function& f) {
      _ISOLATE_CONTEXT_ENTER
        return objectRef(o)->InstanceOf(GetIsolate()->GetCurrentContext(), objectRef(f)).ToChecked();
    }

    jsi::Runtime::PointerValue* V8Runtime::makeStringValue(v8::Local<v8::String> string) const {
      return new V8StringValue(string);
    }

    jsi::String V8Runtime::createString(v8::Local<v8::String> str) const {
      return make<jsi::String>(makeStringValue(str));
    }

    jsi::PropNameID V8Runtime::createPropNameID(v8::Local<v8::Value> str) {
      _ISOLATE_CONTEXT_ENTER
        return make<jsi::PropNameID>(makeStringValue(v8::Local<v8::String>::Cast(str)));
    }

    jsi::Runtime::PointerValue* V8Runtime::makeObjectValue(v8::Local<v8::Object> objectRef) const {
      _ISOLATE_CONTEXT_ENTER
        return new V8ObjectValue(objectRef);
    }

    jsi::Object V8Runtime::createObject(v8::Local<v8::Object> obj) const {
      _ISOLATE_CONTEXT_ENTER
        return make<jsi::Object>(makeObjectValue(obj));
    }

    jsi::Value V8Runtime::createValue(v8::Local<v8::Value> value) const {
      _ISOLATE_CONTEXT_ENTER
        if (value->IsInt32()) {
          return jsi::Value(value->Int32Value(GetIsolate()->GetCurrentContext()).ToChecked());
        } if (value->IsNumber()) {
          return jsi::Value(value->NumberValue(GetIsolate()->GetCurrentContext()).ToChecked());
        }
        else if (value->IsBoolean()) {
          return jsi::Value(value->BooleanValue(GetIsolate()->GetCurrentContext()).ToChecked());
        }
        else if (value.IsEmpty() || value->IsNull()) {
          return jsi::Value(nullptr);
        }
        else if (value->IsUndefined()) {
          return jsi::Value();
        }
        else if (value->IsString()) {
          // Note :: Non copy create
          return createString(v8::Local<v8::String>::Cast(value));
        }
        else if (value->IsObject()) {
          return createObject(v8::Local<v8::Object>::Cast(value));
        }
        else {
          // WHAT ARE YOU
          std::abort();
        }
    }

    v8::Local<v8::Value> V8Runtime::valueRef(const jsi::Value& value) {

      v8::EscapableHandleScope handle_scope(v8::Isolate::GetCurrent());

      if (value.isUndefined()) {
        return handle_scope.Escape(v8::Undefined(GetIsolate()));
      }
      else if (value.isNull()) {
        return handle_scope.Escape(v8::Null(GetIsolate()));
      }
      else if (value.isBool()) {
        return handle_scope.Escape(v8::Boolean::New(GetIsolate(), value.getBool()));
      }
      else if (value.isNumber()) {
        return handle_scope.Escape(v8::Number::New(GetIsolate(), value.getNumber()));
      }
      else if (value.isString()) {
        return handle_scope.Escape(stringRef(value.asString(*this)));
      }
      else if (value.isObject()) {
        return handle_scope.Escape(objectRef(value.getObject(*this)));
      }
      else {
        // What are you?
        std::abort();
      }
    }

    v8::Local<v8::String> V8Runtime::stringRef(const jsi::String& str) {
      v8::EscapableHandleScope handle_scope(v8::Isolate::GetCurrent());
      const V8StringValue* v8StringValue = static_cast<const V8StringValue*>(getPointerValue(str));
      return handle_scope.Escape(v8StringValue->v8String_.Get(v8::Isolate::GetCurrent()));
    }

    v8::Local<v8::Value> V8Runtime::valueRef(const jsi::PropNameID& sym) {
      v8::EscapableHandleScope handle_scope(v8::Isolate::GetCurrent());
      const V8StringValue* v8StringValue = static_cast<const V8StringValue*>(getPointerValue(sym));
      return handle_scope.Escape(v8StringValue->v8String_.Get(v8::Isolate::GetCurrent()));
    }

    v8::Local<v8::Object> V8Runtime::objectRef(const jsi::Object& obj) {
      v8::EscapableHandleScope handle_scope(v8::Isolate::GetCurrent());
      const V8ObjectValue* v8ObjectValue = static_cast<const V8ObjectValue*>(getPointerValue(obj));
      return handle_scope.Escape(v8ObjectValue->v8Object_.Get(v8::Isolate::GetCurrent()));
    }

    std::unique_ptr<jsi::Runtime> makeV8Runtime() {
      return std::make_unique<V8Runtime>();
    }
  }
} // namespace facebook::v8runtime
