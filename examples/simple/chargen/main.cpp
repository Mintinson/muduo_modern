#include "chaoxi/base/Logging.hpp"
#include "chargen.hpp"
#include "chaoxi/net/EventLoop.hpp"


#include <unistd.h>



int main()
{
  LOG_INFO << "pid = " << getpid();
  chaoxi::net::EventLoop loop;
  chaoxi::net::InetAddress listenAddr(2019);
  ChargenServer server(&loop, listenAddr, true);
  server.start();
  loop.loop();
}
