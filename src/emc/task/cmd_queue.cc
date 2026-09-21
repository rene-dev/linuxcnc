/********************************************************************
* Description: cmd_queue.cc
*   Command injection point for task-internal command producers.
*   See cmd_queue.hh for what it is for.
*
* License: GPL Version 2
********************************************************************/

#include "cmd_queue.hh"

#include "nml_intf/emc.hh"
#include "nml_intf/emc_nml.hh"

#include <cstring>
#include <deque>
#include <mutex>
#include <vector>

namespace {

struct Entry {
    std::vector<char>  data;    /* the message, byte for byte */
    taskcmd::Ticket    ticket = 0;
    unsigned long      key = 0; /* 0: never superseded */
};

std::mutex         g_mu;        /* guards everything below */
std::deque<Entry>  g_queue;
Entry              g_current;   /* keeps the popped message alive */
taskcmd::Ticket    g_next_ticket = 1;

/* The command task has dispatched but not settled yet, and the state
   changes waiting to be drained. */
taskcmd::Ticket             g_inflight = 0;
std::vector<taskcmd::Event> g_events;

/* A backlog this long means the producer is far ahead of task and the
   commands are stale anyway; dropping is better than growing without
   bound. Jogs coalesce, so reaching this takes a genuine flood. */
const size_t MAX_QUEUED = 64;

/* Events pile up when nobody drains them, which is the normal case: halui
   never asks, and there may be no websocket client connected. Keeping only
   the newest is right -- a client that connects later has no business
   learning about commands issued before it existed. */
const size_t MAX_EVENTS = 256;

/* g_mu must be held. */
void note(taskcmd::Ticket ticket, taskcmd::State state)
{
    if (ticket == 0) {
        return;
    }
    if (g_events.size() >= MAX_EVENTS) {
        g_events.erase(g_events.begin());
    }
    g_events.push_back(taskcmd::Event{ticket, state});
}

} // namespace

taskcmd::Ticket taskcmd::pushRaw(const void *cmd, size_t size,
                                 unsigned long coalesce_key)
{
    if (cmd == NULL || size == 0 || size > MAX_CMD_SIZE) {
        return 0;
    }

    std::lock_guard<std::mutex> lk(g_mu);

    const Ticket ticket = g_next_ticket++;

    if (coalesce_key != 0) {
        for (Entry &e : g_queue) {
            if (e.key != coalesce_key) {
                continue;
            }
            /* Supersede in place: the newer command takes the older one's
               turn, so ordering against other commands is unchanged. */
            e.data.assign(static_cast<const char *>(cmd),
                          static_cast<const char *>(cmd) + size);
            note(e.ticket, State::done);   /* superseded, never ran */
            e.ticket = ticket;
            return ticket;
        }
    }

    if (g_queue.size() >= MAX_QUEUED) {
        return 0;
    }

    Entry e;
    e.data.assign(static_cast<const char *>(cmd),
                  static_cast<const char *>(cmd) + size);
    e.ticket = ticket;
    e.key = coalesce_key;
    g_queue.push_back(std::move(e));

    return ticket;
}

bool taskcmd::pending(Ticket ticket)
{
    if (ticket == 0) {
        return false;
    }

    std::lock_guard<std::mutex> lk(g_mu);
    for (const Entry &e : g_queue) {
        if (e.ticket == ticket) {
            return true;
        }
    }
    return false;
}

RCS_CMD_MSG *taskcmd::next(int echo_serial_number)
{
    std::lock_guard<std::mutex> lk(g_mu);

    if (g_queue.empty()) {
        return NULL;
    }

    /* Task takes one command per cycle whatever the last one is doing, just
       as it does with NML. A command still in flight has therefore been
       left behind: report it done, which is what an NML client concludes
       when the echo moves past its serial. */
    if (g_inflight != 0) {
        note(g_inflight, State::done);
        g_inflight = 0;
    }

    g_current = std::move(g_queue.front());
    g_queue.pop_front();

    g_inflight = g_current.ticket;
    note(g_inflight, State::received);

    /* Task treats a command as new when its serial differs from the one it
       last echoed, so hand it the next serial along. */
    RCS_CMD_MSG *msg = reinterpret_cast<RCS_CMD_MSG *>(g_current.data.data());
    msg->serial_number = echo_serial_number + 1;
    return msg;
}

void taskcmd::report(RCS_STATUS status)
{
    std::lock_guard<std::mutex> lk(g_mu);

    if (g_inflight == 0) {
        return;
    }
    /* EXEC means the command is still running, so leave it in flight. */
    if (status == RCS_STATUS::DONE) {
        note(g_inflight, State::done);
        g_inflight = 0;
    } else if (status == RCS_STATUS::ERROR) {
        note(g_inflight, State::error);
        g_inflight = 0;
    }
}

void taskcmd::drainEvents(std::vector<Event> &out)
{
    out.clear();

    std::lock_guard<std::mutex> lk(g_mu);
    out.swap(g_events);
}
