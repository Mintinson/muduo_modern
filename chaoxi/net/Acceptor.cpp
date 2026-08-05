
#include "chaoxi/net/Acceptor.hpp"

#include "chaoxi/base/Logging.hpp"
#include "chaoxi/base/Timestamp.hpp"
#include "chaoxi/net/SocketOps.hpp"

#include <cassert>

#include <errno.h>
#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#endif

namespace chaoxi::net
{
Acceptor::Acceptor(EventLoop* loop,
                   const InetAddress& listenAddr,
                   bool reuseport)
    : loop_(loop)
    , acceptSocket_(sockets::createNonblockingOrDie(listenAddr.family()))
    , acceptChannel_(loop_, acceptSocket_.fd())
    , listening_(false)
    , idleFd_(
#ifdef _WIN32
          -1
#else
          ::open("/dev/null", O_RDONLY | O_CLOEXEC)
#endif
      )
{
#ifndef _WIN32
    assert(idleFd_ >= 0);
#endif
    acceptSocket_.setReuseAddr(true);
    acceptSocket_.setReusePort(reuseport);
    acceptSocket_.bindAddress(listenAddr);
    acceptChannel_.setReadCallback([this](Timestamp) { handleRead(); });
}

Acceptor::~Acceptor()
{
    acceptChannel_.disableAll();
    acceptChannel_.remove();
#ifndef _WIN32
    ::close(idleFd_);
#endif
}

void Acceptor::listen()
{
    loop_->assertInLoopThread();
    listening_ = true;
    acceptSocket_.listen();
    acceptChannel_.enableReading();
}

void Acceptor::handleRead()
{
    loop_->assertInLoopThread();

    InetAddress peerAddr;

    SocketHandle condfd = acceptSocket_.accept(&peerAddr);

    if (condfd != kInvalidSocket)
    {
        if (newConnectionCallback_)
        {
            newConnectionCallback_(condfd, peerAddr);
        }
        else
        {
            sockets::close(condfd);
        }
    }
    else
    {
        LOG_SYSERR << "in Acceptor::handleRead";
        // Read the section named "The special problem of
        // accept()ing when you can't" in libev's doc.
        // By Marc Lehmann, author of libev.
        if (errno == EMFILE)
        {
#ifndef _WIN32
            ::close(idleFd_);
            idleFd_ = ::accept(acceptSocket_.fd(), NULL, NULL);
            ::close(idleFd_);
            idleFd_ = ::open("/dev/null", O_RDONLY | O_CLOEXEC);
#endif
        }
    }
}

}  // namespace chaoxi::net
