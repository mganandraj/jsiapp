#pragma once

#include <stddef.h>

namespace v8 {
	class Platform;
	template<typename T>
	class Local;
	class Value;
	class Message;
}  // namespace v8

namespace node {
	namespace inspector {

		class AgentImpl;

		class Agent {
		public:
			explicit Agent();
			~Agent();

			// Start the inspector agent thread
			bool Start(v8::Platform* platform, const char* path, int port, bool wait);
			// Stop the inspector agent
			void Stop();

			bool IsStarted();
			bool IsConnected();
			void WaitForDisconnect();
			void FatalException(v8::Local<v8::Value> error,
				v8::Local<v8::Message> message);
		private:
			AgentImpl* impl;
		};

	}  // namespace inspector
}  // namespace node
