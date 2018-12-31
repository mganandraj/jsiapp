#pragma once

// #include "util-inl.h"
// #include "uv.h"

#include <string>
#include <vector>

#include <tcp.h>

template <typename T, void(*function)(T*)>
struct FunctionDeleter {
  void operator()(T* pointer) const { function(pointer); }
  typedef std::unique_ptr<T, FunctionDeleter> Pointer;
};

template <typename T, void(*function)(T*)>
using DeleteFnPtr = typename FunctionDeleter<T, function>::Pointer;

namespace node {
namespace inspector {

class ProtocolHandler;

// HTTP Wrapper around a uv_tcp_t
class InspectorSocket {
 public:
  class Delegate {
   public:
    virtual void OnHttpGet(const std::string& host,
                           const std::string& path) = 0;
    virtual void OnSocketUpgrade(const std::string& host,
                                 const std::string& path,
                                 const std::string& accept_key) = 0;
    virtual void OnWsFrame(const std::vector<char>& frame) = 0;
    virtual ~Delegate() {}
  };

  using DelegatePointer = std::unique_ptr<Delegate>;
  using Pointer = std::unique_ptr<InspectorSocket>;

  static Pointer Accept(tcp_connection::pointer socket, DelegatePointer delegate);

  ~InspectorSocket();

  void AcceptUpgrade(const std::string& accept_key);
  void CancelHandshake();
  void Write(const char* data, size_t len);
  void SwitchProtocol(ProtocolHandler* handler);
  std::string GetHost();

 private:
  static void Shutdown(ProtocolHandler*);
  InspectorSocket() = default;

  DeleteFnPtr<ProtocolHandler, Shutdown> protocol_handler_;

  InspectorSocket(const InspectorSocket&) = delete;
  InspectorSocket& operator=(const InspectorSocket&) = delete;
};


}  // namespace inspector
}  // namespace node