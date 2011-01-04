/* Copyright (c) 2010 Stanford University
 *
 * Permission to use, copy, modify, and distribute this software for any purpose
 * with or without fee is hereby granted, provided that the above copyright
 * notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR(S) DISCLAIM ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL AUTHORS BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER
 * RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF
 * CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "Common.h"
#include "TcpTransport.h"

namespace RAMCloud {

/**
 * Default object used to make system calls.
 */
static Syscall defaultSyscall;

/**
 * Used by this class to make all system calls.  In normal production
 * use it points to defaultSyscall; for testing it points to a mock
 * object.
 */
Syscall* TcpTransport::sys = &defaultSyscall;

/**
 * Used by libevent to manage events; shared among all TcpTransport
 * instances.  NULL means the event system hasn't yet been initialized.
 */
event_base* eventBase = NULL;
bool init = false;

/**
 * Construct a TcpTransport instance.
 * \param serviceLocator
 *      If non-NULL this transport will be used to serve incoming
 *      RPC requests as well as make outgoing requests; this parameter
 *      specifies the (local) address on which to listen for connections.
 *      If NULL this transport will be used only for outgoing requests.
 */
TcpTransport::TcpTransport(const ServiceLocator* serviceLocator)
        : locatorString(), listenSocket(-1), listenSocketEvent(), sockets(),
        waitingRequests()
{
    if (!init) {
        eventBase = event_init();
        init = true;
    }
    if (serviceLocator == NULL)
        return;
    IpAddress address(*serviceLocator);
    locatorString = serviceLocator->getOriginalString();

    listenSocket = sys->socket(PF_INET, SOCK_STREAM, 0);
    if (listenSocket == -1) {
        throw TransportException(HERE,
                "TcpTransport couldn't create listen socket", errno);
    }

    int r = sys->fcntl(listenSocket, F_SETFL, O_NONBLOCK);
    if (r != 0) {
        throw TransportException(HERE,
                "TcpTransport couldn't set nonblocking on listen socket",
                errno);
    }

    int optval = 1;
    if (sys->setsockopt(listenSocket, SOL_SOCKET, SO_REUSEADDR, &optval,
                           sizeof(optval)) != 0) {
        throw TransportException(HERE,
                "TcpTransport couldn't set SO_REUSEADDR on listen socket",
                errno);
    }

    if (sys->bind(listenSocket, &address.address,
            sizeof(address.address)) == -1) {
        // destructor will close listenSocket
        string message = format("TcpTransport couldn't bind to '%s'",
                serviceLocator->getOriginalString().c_str());
        throw TransportException(HERE, message, errno);
    }

    if (sys->listen(listenSocket, INT_MAX) == -1) {
        // destructor will close listenSocket
        throw TransportException(HERE,
                "TcpTransport couldn't listen on socket", errno);
    }

    // Arrange to be notified whenever anyone connects to listenSocket.
    event_set(&listenSocketEvent, listenSocket, EV_READ|EV_PERSIST,
            tryAccept, this);
    if (event_add(&listenSocketEvent, NULL) != 0) {
        throw TransportException(HERE,
                "event_add failed in TcpTransport constructor");
    }
}

/**
 * Destructor for TcpTransport's: close file descriptors and perform
 * any other needed cleanup.
 */
TcpTransport::~TcpTransport()
{
    if (listenSocket >= 0) {
        event_del(&listenSocketEvent);
        sys->close(listenSocket);
        listenSocket = -1;
    }
    for (unsigned int i = 0; i < sockets.size(); i++) {
        if (sockets[i] != NULL) {
            closeSocket(i);
        }
    }
}

/**
 * This private method is invoked to close the server's end of a
 * connection to a client and cleanup any related state.
 * \param fd
 *      File descriptor for the socket to be closed.
 */
void
TcpTransport::closeSocket(int fd) {
    event_del(&sockets[fd]->readEvent);
    sys->close(fd);
    sockets[fd]->busy = false;
    sockets[fd]->rpc = NULL;
    sockets[fd] = NULL;
}

/**
 * This method is invoked by libevent when listenSocket becomes readable;
 * it tries to open a new connection with a client. If that succeeds
 * then we will begin listening on that socket for RPCs.
 *
 * \param fd
 *      The file descriptor that became readable (should be the same
 *      as listenSocket).
 * \param event
 *      The event that happened: should always be EV_READ.
 * \param arg
 *      Pointer to the TcpTransport object whose listenSocket became
 *      readable.
 */
void
TcpTransport::tryAccept(int fd, int16_t event, void* arg)
{
    TcpTransport* transport = static_cast<TcpTransport*>(arg);

    // If you opted out of listening in the constructor,
    // you're not allowed to accept now.
    assert(transport->listenSocket > 0);

    int acceptedFd = sys->accept(transport->listenSocket, NULL, NULL);
    if (acceptedFd < 0) {
        switch (errno) {
            // According to the man page for accept, you're supposed to
            // treat these as retry on Linux.
            case EHOSTDOWN:
            case EHOSTUNREACH:
            case ENETDOWN:
            case ENETUNREACH:
            case ENONET:
            case ENOPROTOOPT:
            case EOPNOTSUPP:
            case EPROTO:
                return;

            // No incoming connections are currently available.
            case EAGAIN:
#if EAGAIN != EWOULDBLOCK
            case EWOULDBLOCK:
#endif
                return;
        }

        // Unexpected error: log a message and then close the socket
        // (so we don't get repeated errors).
        LOG(ERROR, "error in TcpTransport accepting connection "
                "for '%s': %s", transport->locatorString.c_str(),
                strerror(errno));
        event_del(&transport->listenSocketEvent);
        sys->close(transport->listenSocket);
        transport->listenSocket = -1;
        return;
    }

    // At this point we have successfully opened a client connection.
    // Save information about it and create a handler for incoming
    // requests.
    if (transport->sockets.capacity() <=
            static_cast<unsigned int>(acceptedFd)) {
        transport->sockets.resize(acceptedFd + 1);
    }
    Socket* socket = transport->sockets[acceptedFd] = new Socket;
    socket->rpc =  NULL;
    socket->busy = false;
    event_set(&socket->readEvent, acceptedFd, EV_READ|EV_PERSIST,
            tryServerRecv, transport);
    if (event_add(&socket->readEvent, NULL) != 0) {
        throw TransportException(HERE,
                "event_add failed in TcpTransport::tryAccept");
    }
}

/**
 * This method is invoked when a client socket becomes readable. It
 * attempts to read an incoming message from the socket. If a full
 * message is available, a TcpServerRpc object gets queued for service.
 *
 * \param fd
 *      File descriptor that is now readable.
 * \param event
 *      The event that happened: should always be EV_READ.
 * \param arg
 *      Pointer to TcpTransport object associated with the socket.
 */
void
TcpTransport::tryServerRecv(int fd, int16_t event, void *arg)
{
    TcpTransport* transport = static_cast<TcpTransport*>(arg);

    Socket* socket = transport->sockets[fd];
    assert(socket != NULL);
    if (socket->rpc == NULL) {
        socket->rpc = new TcpServerRpc(socket, fd,
                &socket->rpc->recvPayload);
        socket->rpc->message.reset(&socket->rpc->recvPayload);
    }
    try {
        if (socket->busy) {
            // We're already working on serving an RPC from the socket
            // so were not expecting more data now. There are 2 possible
            // explanations: either the client closed its socket, or it
            // sent us data when it shouldn't have (or perhaps sent too
            // long a request message?). Try to read the unwanted data.
            // If it doesn't throw TcpTransportEof and there is actually
            // data, discard the data.
            char buffer[2000];
            ssize_t length = TcpTransport::recvCarefully(fd, buffer,
                    sizeof(buffer));
            if (length > 0) {
                LOG(WARNING, "TcpTransport discarding %d unexpected bytes "
                        "from client", static_cast<int>(length));
            }
            return;
        }
        if (socket->rpc->message.readMessage(fd)) {
            // The incoming request is complete; make it available
            // for servicing.
            transport->waitingRequests.push(socket->rpc);
            socket->rpc = NULL;
            socket->busy = true;
        }
    } catch (TcpTransportEof& e) {
        // Close the socket in order to prevent an infinite loop of
        // calls to this method.
        transport->closeSocket(fd);
    } catch (TransportException& e) {
        LOG(ERROR, "TcpTransport closing client connection: %s",
                e.message.c_str());
        transport->closeSocket(fd);
    }
}

/**
 * Transmit an RPC request or response on a socket; this method does
 * not return until the kernel has accepted all of the data.
 *
 * \param fd
 *      File descriptor to write (must be in blocking mode)
 * \param payload
 *      Message to transmit on fd; this method adds on a header.
 *
 * \throw TransportException
 *      An I/O error occurred.
 */
void
TcpTransport::sendMessage(int fd, Buffer& payload)
{
    assert(fd >= 0);

    Header header;
    header.len = payload.getTotalLength();

    // Use an iovec to send everything in one kernel call: one iov
    // for header, the rest for payload
    uint32_t iovecs = 1 + payload.getNumberChunks();
    struct iovec iov[iovecs];
    iov[0].iov_base = &header;
    iov[0].iov_len = sizeof(header);

    Buffer::Iterator iter(payload);
    int i = 1;
    while (!iter.isDone()) {
        iov[i].iov_base = const_cast<void*>(iter.getData());
        iov[i].iov_len = iter.getLength();
        ++i;
        iter.next();
    }

    struct msghdr msg;
    memset(&msg, 0, sizeof(msg));
    msg.msg_iov = iov;
    msg.msg_iovlen = iovecs;

    ssize_t r = sys->sendmsg(fd, &msg, MSG_NOSIGNAL);
    if (static_cast<size_t>(r) != (sizeof(header) + header.len)) {
        if (r == -1) {
            throw TransportException(HERE,
                    "I/O error in TcpTransport::sendMessage", errno);
        }
        throw TransportException(HERE, format("Incomplete sendmsg in "
                "TcpTransport::sendMessage: %d bytes sent out of %d",
                static_cast<int>(r),
                static_cast<int>(sizeof(header) + header.len)));
    }
}

/**
 * Read bytes from a socket and generate exceptions for errors and
 * end-of-file.
 *
 * \param fd
 *      File descriptor for socket.
 * \param buffer
 *      Store incoming data here.
 * \param length
 *      Maximum number of bytes to read.
 * \return
 *      The number of bytes read.  The recv is done in non-blocking mode;
 *      if there are no bytes available then 0 is returned (0 does *not*
 *      mean end-of-file).
 *
 * \throw TransportException
 *      An I/O error occurred.
 * \throw TcpTransportEof
 *      The other side closed the connection.
 */

ssize_t
TcpTransport::recvCarefully(int fd, void* buffer, size_t length) {
    ssize_t actual = sys->recv(fd, buffer, length, MSG_DONTWAIT);
    if (actual > 0) {
        return actual;
    }
    if (actual == 0) {
        throw TcpTransportEof(HERE);
    }
    if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
        return 0;
    }
    throw TransportException(HERE, "I/O read error in TcpTransport", errno);
}

/**
 * Constructor for IncomingMessages.
 * \param buffer
 *      The contents of the message (not including transport-specific
 *      header) will be placed in this buffer; caller should ensure that
 *      the buffer is empty.
 */
TcpTransport::IncomingMessage::IncomingMessage(Buffer* buffer)
    : header(), headerBytesReceived(0), messageBytesReceived(0),
    buffer(buffer)
{
}

/**
 * This method is called before beginning a message transfer to
 * reinitialize the state of the object.
 * \param buffer
 *      The contents of the message (not including transport-specific
 *      header) will be placed in this buffer; caller should ensure that
 *      the buffer is empty.
 */
void TcpTransport::IncomingMessage::reset(Buffer* buffer)
{
    headerBytesReceived = 0;
    messageBytesReceived= 0;
    this->buffer = buffer;
}

/**
 * Attempt to read part or all of a message from an open socket.
 *
 * \param fd
 *      File descriptor to use for reading message info.
 * \return
 *      True means the message is complete (it's present in the
 *      buffer provided to the constructor); false means we still need
 *      more data.
 *
 * \throw TransportException
 *      An I/O error occurred.
 * \throw TcpTransportEof
 *      The other side closed the connection.
 */

bool
TcpTransport::IncomingMessage::readMessage(int fd) {
    // First make sure we have received the header (it may arrive in
    // multiple chunks).
    while (headerBytesReceived < sizeof(Header)) {
        ssize_t len = TcpTransport::recvCarefully(fd,
                reinterpret_cast<char*>(&header) + headerBytesReceived,
                sizeof(header) - headerBytesReceived);
        if (len <= 0)
            return false;
        headerBytesReceived += len;
    }

    if (header.len > MAX_RPC_LEN) {
        throw TransportException(HERE,
                format("TcpTransport received oversize message (%d bytes)",
                header.len));
    }

    if (header.len == 0)
        return true;

    // We have the header; now receive the message body (it may take several
    // calls to this method before we get all of it).
    {
        void *dest;
        if (buffer->getTotalLength() == 0) {
            dest = new(buffer, APPEND) char[header.len];
        } else {
            buffer->peek(messageBytesReceived,
                    const_cast<const void**>(&dest));
        }
        ssize_t len = TcpTransport::recvCarefully(fd, dest,
                header.len - messageBytesReceived);
        if (len <= 0)
            return false;
        messageBytesReceived += len;
    }
    return messageBytesReceived == header.len;
}

/**
 * Construct a TcpSession object for communication with a given server.
 *
 * \param serviceLocator
 *      Identifies the server to which RPCs on this session will be sent.
 */
TcpTransport::TcpSession::TcpSession(const ServiceLocator& serviceLocator)
        : address(serviceLocator), fd(-1), current(NULL), message(NULL),
        readEvent(), errorInfo()
{
    fd = sys->socket(PF_INET, SOCK_STREAM, 0);
    if (fd == -1) {
        throw TransportException(HERE,
                "TcpTransport couldn't open socket for session", errno);
    }

    int r = sys->connect(fd, &address.address, sizeof(address.address));
    if (r == -1) {
        sys->close(fd);
        fd = -1;
        throw TransportException(HERE,
                "Session connect error in TcpTransport", errno);
    }

    /// Arrange for notification whenever the server sends us data.
    event_set(&readEvent, fd, EV_READ|EV_PERSIST, TcpSession::tryReadReply,
            this);
    if (event_add(&readEvent, NULL) != 0) {
        throw TransportException(HERE,
                "event_add failed in TcpSession constructor");
    }
}

/**
 * Destructor for TcpSession objects.
 */
TcpTransport::TcpSession::~TcpSession()
{
    close();
}

/**
 * Close the socket associated with a session.
 */
void
TcpTransport::TcpSession::close()
{
    if (fd >= 0) {
        event_del(&readEvent);
        sys->close(fd);
        fd = -1;
    }
}

// See Transport::Session::clientSend for documentation.
TcpTransport::ClientRpc*
TcpTransport::TcpSession::clientSend(Buffer* request, Buffer* reply)
{
    while (current != NULL) {
        // We can't handle more than one outstanding request at a time;
        // wait for the previous request to complete.
        event_loop(EVLOOP_ONCE);
    }
    if (fd == -1) {
        throw TransportException(HERE, errorInfo);
    }
    current = new(reply, MISC) TcpClientRpc(this, reply);
    message.reset(reply);
    TcpTransport::sendMessage(fd, *request);
    return current;
}

/**
 * This method is invoked when the socket connecting to a server becomes
 * readable (because a reply is arriving). This method reads the reply.
 *
 * \param fd
 *      File descriptor that just became readable.
 * \param event
 *      The event that happened: should always be EV_READ.
 * \param arg
 *      Pointer to the TcpSession associated with fd.
 */
void
TcpTransport::TcpSession::tryReadReply(int fd, int16_t event, void *arg)
{
    TcpSession* session = static_cast<TcpSession*>(arg);
    try {
        if (session->current == NULL) {
            // We can get here under 2 conditions: either the server closed
            // its connection (most likely), or it violated the protocol and
            // sent us data when there was no request pending.  Try to read
            // data (most likely generating TcpTransportEof); if there is any
            // data, discard it.
            char buffer[2000];
            ssize_t length = TcpTransport::recvCarefully(fd, buffer,
                    sizeof(buffer));
            if (length > 0) {
                LOG(WARNING, "TcpTransport discarding %d unexpected bytes "
                        "from server %s", static_cast<int>(length),
                        session->address.toString().c_str());
            }
            return;
        }
        if (session->message.readMessage(fd)) {
            // This RPC is finished.
            session->current->finished = true;
            session->current = NULL;
        }
    } catch (TcpTransportEof& e) {
        // Close the session's socket in order to prevent an infinite loop of
        // calls to this method.
        session->close();
        session->current = NULL;
        session->errorInfo = "socket closed by server";
    } catch (TransportException& e) {
        LOG(ERROR, "TcpTransport closing session socket: %s",
                e.message.c_str());
        session->close();
        session->current = NULL;
        session->errorInfo = e.message;
    }
}

// See Transport::ClientRpc::isReady for documentation.
bool
TcpTransport::TcpClientRpc::isReady()
{
    if (finished || session->fd == -1)
        return true;
    event_loop(EVLOOP_NONBLOCK);
    return (finished || session->fd == -1);
}

// See Transport::ClientRpc::wait for documentation.
void
TcpTransport::TcpClientRpc::wait()
{
    while (!finished) {
        if (session->fd == -1)
            throw TransportException(HERE, session->errorInfo);
        event_loop(EVLOOP_ONCE);
    }
}

// See Transport::serverRecv for documentation.
Transport::ServerRpc*
TcpTransport::serverRecv()
{
    if (waitingRequests.empty()) {
        event_loop(EVLOOP_ONCE|EVLOOP_NONBLOCK);
        if (waitingRequests.empty())
            return NULL;
    }
    ServerRpc* result = waitingRequests.front();
    waitingRequests.pop();
    return result;
}

// See Transport::ServerRpc::sendReply for documentation.
void
TcpTransport::TcpServerRpc::sendReply()
{
    assert(socket->busy);

    // "delete this;" on our way out of the method
    std::auto_ptr<TcpServerRpc> suicide(this);

    TcpTransport::sendMessage(fd, replyPayload);
    socket->busy = false;
}

}  // namespace RAMCloud
