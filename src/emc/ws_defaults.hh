/********************************************************************
* Description: ws_defaults.hh
*   Settings shared by the websocket server in task and the clients that
*   connect to it. Kept out of both so that neither has to include the
*   other's header just to agree on a port number.
*
* License: GPL Version 2
********************************************************************/
#ifndef WS_DEFAULTS_HH
#define WS_DEFAULTS_HH

/* The port task listens on unless [TASK]WEBSOCKET_PORT says otherwise, and
   the port a client connects to unless told another one. Setting it to 0 in
   the INI file turns the server off; a client then cannot connect at all.

   5700 is in the IANA dynamic range and is not registered to anything. */
#define WS_DEFAULT_PORT 5700

/* Where a client looks for task, unless told otherwise. The server binds
   loopback only. */
#define WS_DEFAULT_HOST "127.0.0.1"

#endif /* WS_DEFAULTS_HH */
