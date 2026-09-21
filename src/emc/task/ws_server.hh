/********************************************************************
* Description: ws_server.hh
*   WebSocket + FlatBuffers transport for task <-> UI communication.
*
*   This is the whole interface task sees. Everything else -- Boost.Beast,
*   the FlatBuffers encoding, the test web UI -- lives in ws_server.cc so
*   that emctaskmain.cc stays free of it.
*
* License: GPL Version 2
********************************************************************/
#ifndef WS_SERVER_HH
#define WS_SERVER_HH

class EMC_STAT;

/* Start the server thread listening on 127.0.0.1:port.
   Returns 0 on success, -1 on failure (task carries on without it).
   A port <= 0 disables the server entirely. */
int wsServerStart(int port);

/* Stop the server and join its thread. Safe to call if never started. */
void wsServerStop(void);

/* Hand the current status to the server. Called once per task cycle.
   Cheap by design: this only memcpy's EMC_STAT under a short lock -- the
   FlatBuffers encoding happens on the server thread, off the task loop. */
void wsServerPublish(const EMC_STAT *status);

/* Kinds of operator message, matching EMC_OPERATOR_ERROR/TEXT/DISPLAY. */
enum {
    WS_OPERATOR_ERROR   = 0,
    WS_OPERATOR_TEXT    = 1,
    WS_OPERATOR_DISPLAY = 2
};

/* Relay one operator message to every connected client, replacing the
   emcError NML channel. Call it where task writes that channel.

   Cheap by design, like wsServerPublish(): this only copies the text under
   a short lock and the message goes out with the next status publish, i.e.
   on the next task cycle. */
void wsServerOperatorMsg(int kind, const char *text);

/* Commands received over a websocket are not returned here: they go into
   the task-internal command queue (cmd_queue.hh), which the task loop
   drains alongside the NML command channel. Their outcome goes back to the
   client that sent them as a CmdAck, built from the state changes
   taskcmd::drainEvents() reports -- which is why the task loop has to call
   taskcmd::report() once a cycle. */

#endif /* WS_SERVER_HH */
