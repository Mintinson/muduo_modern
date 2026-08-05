#include "chaoxi/base/Logging.hpp"
#include "chaoxi/base/ProcessInfo.hpp"
#include "chargen.hpp"
#include "chaoxi/net/EventLoop.hpp"





int main()
{
  LOG_INFO << "pid = " << chaoxi::process_info::pid();
  chaoxi::net::EventLoop loop;
  chaoxi::net::InetAddress listenAddr(2019);
  ChargenServer server(&loop, listenAddr, true);
  server.start();
  loop.loop();
}
