#pragma once

// The session runs on its own thread so a slow network never stalls a frame.
// It exchanges data with the game only through Runtime; it never touches the
// engine.

#include "config.hpp"

#include "tandem/protocol.hpp"

#include <memory>

namespace tandem::plugin {
class NetThread {
public:
  NetThread(Config config, proto::Body body);
  ~NetThread(); // stops the thread, says goodbye to the session

  NetThread(const NetThread &) = delete;
  NetThread &operator=(const NetThread &) = delete;

private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};
} // namespace tandem::plugin
