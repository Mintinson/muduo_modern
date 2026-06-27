#include "chaoxi/base/AsyncLogging.hpp"
#include "chaoxi/base/Logging.hpp"
#include "chaoxi/base/Timestamp.hpp"
#include <chrono>
#include <print>
#include <ratio>
#include <string_view>

#include <stdio.h>
#include <sys/resource.h>
#include <unistd.h>

off_t kRollSize = 500*1000*1000;

chaoxi::AsyncLogging* g_asyncLog = NULL;

void asyncOutput(std::string_view msg)
{
  g_asyncLog->append(msg);
}

void bench(bool longLog)
{
  chaoxi::Logger::setOutput(asyncOutput);

  int cnt = 0;
  const int kBatch = 1000;
  std::string empty = " ";
  std::string longStr(3000, 'X');
  longStr += " ";

  for (int t = 0; t < 30; ++t)
  {
    chaoxi::Timestamp start = chaoxi::Timestamp::clock::now();
    for (int i = 0; i < kBatch; ++i)
    {
      LOG_INFO << "Hello 0123456789" << " abcdefghijklmnopqrstuvwxyz "
               << (longLog ? longStr : empty)
               << cnt;
      ++cnt;
    }
    chaoxi::Timestamp end = chaoxi::Timestamp::clock::now();
    std::println("{}", std::chrono::duration<double, std::micro>(end - start).count() / kBatch);
    // printf("%f\n", timeDifference(end, start)*1000000/kBatch);
    struct timespec ts = { 0, 500*1000*1000 };
    nanosleep(&ts, NULL);
  }
}

int main(int argc, char* argv[])
{
  {
    // set max virtual memory to 2GB.
    size_t kOneGB = 1000*1024*1024;
    rlimit rl = { 2*kOneGB, 2*kOneGB };
    setrlimit(RLIMIT_AS, &rl);
  }

  printf("pid = %d\n", getpid());

  char name[256] = { '\0' };
  strncpy(name, argv[0], sizeof name - 1);
  chaoxi::AsyncLogging log(::basename(name), kRollSize);
  log.start();
  g_asyncLog = &log;

  bool longLog = argc > 1;
  bench(longLog);
}
