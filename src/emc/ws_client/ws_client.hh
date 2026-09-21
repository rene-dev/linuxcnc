/********************************************************************
* Description: ws_client.hh
*   Client side of the websocket + FlatBuffers transport to task.
*
*   This is the counterpart of src/emc/task/ws_server.cc and replaces the
*   three NML channels a user interface used to open: emcCommand,
*   emcStatus and emcError all arrive on one socket.
*
*   The interface is deliberately NML-shaped -- it hands out an EMC_STAT
*   and takes RCS_CMD_MSG commands -- so that a UI moves across by changing
*   how it connects, not by rewriting every field access. The wire format
*   underneath is FlatBuffers; nothing of NML's is on it.
*
*   Two differences from the NML channels are worth knowing about:
*
*   - A command is identified by a serial this client hands out, and its
*     outcome comes back as an acknowledgement addressed to this connection
*     alone. With NML every client shared one echo_serial_number and one
*     status word, so a UI could see another UI's error as its own. Here it
*     cannot.
*
*   - Operator messages are broadcast. NML's error channel was read-once:
*     with two UIs attached, whichever read first took the message and the
*     other never saw it. Every client gets every message now.
*
* License: GPL Version 2
********************************************************************/
#ifndef WS_CLIENT_HH
#define WS_CLIENT_HH

#include <memory>
#include <string>

class EMC_STAT;
class RCS_CMD_MSG;

namespace linuxcnc {

/* One message off what used to be the emcError NML channel. */
struct OperatorMessage {
    enum class Kind { error, text, display };
    Kind        kind;
    std::string text;
};

/* How far a command has got. */
enum class CmdState {
    unknown,   /* no such serial: never sent, or long since forgotten */
    pending,   /* sent, task has not taken it off its queue yet */
    received,  /* task has dispatched it and is working on it */
    done,      /* task settled with DONE after running it */
    error,     /* task settled with ERROR after running it */
    refused    /* task never saw it: not understood, or its queue was full */
};

class WsClient {
public:
    WsClient();
    ~WsClient();
    WsClient(const WsClient &) = delete;
    WsClient &operator=(const WsClient &) = delete;

    /* Connect. Returns 0 once the websocket handshake has succeeded; it
       does not wait for a first status frame, so that connecting costs no
       task cycles. One attempt only: a caller that wants to retry while
       task starts up calls this in a loop. */
    int  connect(const std::string &host, int port, double timeout = 10.0);

    /* Wait until a status snapshot has arrived, so that status() is
       non-null. For a caller that hands out a pointer to it. */
    int  waitStatus(double timeout = 10.0);
    void disconnect();
    bool connected() const;

    /* The status as of the last update(). NULL before the first one.

       The pointer stays valid, and the contents stay still, until the next
       update() -- so a caller can read a run of fields and know they came
       from one snapshot, which is more than the NML status buffer ever
       promised.

       Writable, as NML's get_address() was, so that a UI keeping notes in
       its status copy still compiles -- but anything written is overwritten
       by the next update(). */
    EMC_STAT *status() const;

    /* The newest status, decoded into the caller's own EMC_STAT, without
       touching what status() returns. It is for a thread that wants a live
       reading on its own schedule -- the backplot logger samples this with
       the GIL released -- while the owner of this client goes on polling.
       False if nothing has arrived yet. */
    bool snapshot(EMC_STAT &out) const;

    /* Take in everything that has arrived: a newer status snapshot, any
       acknowledgements, any operator messages. Does not block.
       Returns 0, or -1 if the connection has gone. */
    int update();

    /* update(), but wait up to timeout seconds for something to arrive
       first. Returns 0 if anything did, -1 on timeout or disconnection. */
    int updateWait(double timeout);

    /* Send a command. Returns the serial to follow it by, or 0 if it could
       not be sent. The command is copied; the caller's copy is free
       afterwards. */
    unsigned long send(const RCS_CMD_MSG &cmd);

    /* Where a command has got to, as of the last update(). */
    CmdState state(unsigned long serial) const;

    /* The highest serial of ours that task has taken off its queue. It is
       what NML's echo_serial_number meant to a UI -- "how far has task got
       through what I asked for?" -- except that it counts this
       connection's commands rather than every client's. */
    unsigned long echoSerial() const;

    /* Wait for a command to be dispatched / to finish. timeout <= 0 waits
       indefinitely. Returns 0 on success, -1 on timeout, a failed command
       or a lost connection. */
    int waitReceived(unsigned long serial, double timeout);
    int waitDone(unsigned long serial, double timeout);

    /* Take the oldest operator message not yet returned. False if there is
       none. */
    bool nextMessage(OperatorMessage &out);

    /* Why the last call failed, for an error message. */
    const std::string &lastError() const;

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace linuxcnc

#endif /* WS_CLIENT_HH */
