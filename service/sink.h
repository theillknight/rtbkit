/* sink.h                                                          -*- C++ -*-
   Wolfgang Sourdeau, September 2013
   Copyright (c) 2013 Datacratic.  All rights reserved.

   A sink mechanism for writing to input or output "pipes".
 */

#pragma once

#include <functional>
#include <string>

#include "async_event_source.h"
#include "typed_message_channel.h"


namespace Datacratic {

/* OUTPUTSINK

   A sink interface that provides a medium-independent interface for sending
   data. Note that "OutputSink" is a pseudo vitual base class.

   The provider is responsible for making the target resource available
   and for closing it. It also handles thread safety and whether the writes
   are blocking or non-blocking. The provider provides the appropriate
   OutputSink for its operations.
 */

struct OutputSink {
    /* "data" has been received and must be written. Returns the number of
     * bytes. */
    typedef std::function<size_t (const char *, size_t)> OnWrite;
    typedef std::function<void ()> OnClose;

    enum State {
        OPEN,
        CLOSING,
        CLOSED
    };

    OutputSink(const OnWrite & onWrite, const OnClose & onClose = nullptr)
        : state(OPEN), onWrite_(onWrite), onClose_(onClose)
    {}

    /* Write data to the output. Returns true when successful. */
    virtual bool write(std::string && data)
    { return (onWrite_(data.c_str(), data.size()) > 0); }

    bool write(const std::string & data)
    {
        std::string localData(data);
        return write(std::move(localData));
    }

    /* Request the output to be closed and guarantee that "write" will never
       be invoked anymore. May be invoked by both ends. */
    virtual void requestClose()
    { doClose(); }

    enum State state;

    void doClose();

    OnWrite onWrite_;
    OnClose onClose_;
};


/* ASYNCOUTPUTSINK

   A non-blocking output sink that sends data to an open file descriptor. */
struct AsyncFdOutputSink : public AsyncEventSource,
                           public OutputSink {
    typedef std::function<void ()> OnHangup;
    AsyncFdOutputSink(const OnWrite & onWrite,
                      const OnHangup & onHangup,
                      const OnClose & onClose,
                      int bufferSize = 32);
    ~AsyncFdOutputSink();

    void init(int outputFd);

    /* AsyncEventSource interface */
    virtual int selectFd() const
    { return epollFd_; }
    virtual bool processOne();

    /* OutputSink interface */
    virtual bool write(std::string && data);
    virtual void requestClose();

    OnHangup onHangup_;
    void doClose();

private:
    typedef std::function<void (struct epoll_event &)> EpollCallback;

    void addFdOneShot(int fd, EpollCallback & cb, bool writerFd = false);
    void restartFdOneShot(int fd, EpollCallback & cb, bool writerFd = false);
    void removeFd(int fd);
    void close();
    int epollFd_;

    void handleFdEvent(const struct epoll_event & event);
    void handleWakeupEvent(const struct epoll_event & event);
    EpollCallback handleFdEventCb_;
    EpollCallback handleWakeupEventCb_;

    void flushThreadBuffer();
    void flushStdInBuffer();

    int outputFd_;
    int fdReady_;

    ML::Wakeup_Fd wakeup_;
    ML::RingBufferSRMW<std::string> threadBuffer_;

    std::string buffer_;
};


/* INPUTSINK

   A sink that provides a medium-independent interface for receiving data.

   The client is responsible for the resource management. The provider
   returns the appropriate InputSink for its operations.

   An InputSink may write to an OutputSink when piping data between 2 threads
   or file descriptors.
 */

struct InputSink {
    /* Notify that data has been received and transfers it. */
    virtual void notifyReceived(std::string && data)
    { throw ML::Exception("unimplemented"); }

    /* Notify that the input has been closed and that data will not be
       received anymore. */
    virtual void notifyClosed(void)
    { throw ML::Exception("unimplemented"); }
};


/* NULLINPUTSINK

   An InputSink that discards everything.
 */

struct NullInputSink : public InputSink {
    virtual void notifyReceived(std::string && data);
    virtual void notifyClosed();
};


/* CALLBACKINPUTSINK

   An InputSink invoking a callback upon data reception.
 */

struct CallbackInputSink : public InputSink {
    typedef std::function<void(std::string && data)> OnData;
    typedef std::function<void()> OnClose;

    CallbackInputSink(const OnData & onData,
                      const OnClose & onClose = nullptr)
        : onData_(onData), onClose_(onClose)
    {}

    virtual void notifyReceived(std::string && data);
    virtual void notifyClosed();

private:
    OnData onData_;
    OnClose onClose_;
};

} // namespace Datacratic
