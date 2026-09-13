# Running the test suite in parallel

Working notes on the `LINUXCNC_INSTANCE` / `runtests -j` work. Nothing here is
committed.

## Result

Full suite, run-in-place build, 16-core machine, no RT capabilities:

| Command                       | Tests                          | Wall time |
|-------------------------------|--------------------------------|-----------|
| `scripts/runtests tests`      | 297 passed, 3 skipped          | 14m24s    |
| `scripts/runtests -j4 tests`  | 297 passed, 3 skipped          | 5m57s     |
| `scripts/runtests -j8 tests`  | 297 passed, 3 skipped          | **2m49s** |

All runs left no shared memory segments and no lock files behind.

The serial and `-j4` times were measured before the ui-smoke tests were made
parallel-safe, so they include ui-smoke running one at a time. The ui-smoke
quit tests now wait 5–7 s for the GUI to finish starting (see below), which
adds roughly 20 s to a serial run.

Before this work the `-j8` figure was 4m49s: the 8 ui-smoke tests had to run
serially at the end and took 3 minutes of that. In parallel they take about
37 s.

## How it works

`LINUXCNC_INSTANCE=n` (0–1023, unset means 0) shifts everything a session owns
in a system-wide namespace by `n * 16`. Instance 0 keeps every historical key,
port, path and file name, so a machine with one session sees no change.
`runtests -j N` gives each worker its own instance (1..N); plain `runtests`
stays on instance 0.

The definition lives in `src/rtapi/rtapi_instance.h` (C/C++) and
`lib/python/linuxcnc_instance.py` (Python). Both reject a value that is not a
whole number in range rather than falling back to 0, which would silently
share memory with a running session.

## What was shared, and what changed

### Shared memory and realtime

| Resource                                         | Change |
|--------------------------------------------------|--------|
| HAL, UUID, motion, halscope, classicladder, sampler/streamer segments | One offset in `rtapi_shmem_new()` (`src/rtapi/uspace_common.h`). Sampler/streamer go through `hal_stream_*` and are covered too. |
| `rtapi_app` socket (`~/.rtapi_fifo`)             | Instance suffix in `get_fifo_path()` (`src/rtapi/uspace_rtapi_main.cc`), so each instance gets its own `rtapi_app`. |
| NML buffers and their SysV mutex semaphores      | Offset on the key in both `SHMEM` constructors (`src/libnml/buffer/shmem.cc`). |
| NML `TCP=`/`STCP=`/`UDP=` ports                  | Offset where the NML file is parsed (`src/libnml/cms/cms.cc`), which covers server and client together. |
| Tool table `~/.tool.mmap`                        | Per-instance file name (`src/emc/tooldata/tooldata_mmap.cc`). |

### Scripts

| Location | Problem | Change |
|---|---|---|
| `scripts/realtime.in` | `ipcrm` on three hardcoded keys removed instance 0's segments | Keys offset by instance |
| `scripts/realtime.in` | `ps -C rtapi_app` counted every instance's daemon | `rtapi_app ping`, which asks this instance's socket |
| `scripts/linuxcnc.in` | `pidof` sweep on shutdown killed other sessions' `linuxcncsvr`, `milltask`, GUIs | `InstancePidof`, filtered by each process's `LINUXCNC_INSTANCE` in `/proc` |
| `scripts/linuxcnc.in` | NML shutdown `ipcrm` used raw keys from the NML file | Keys offset by instance |
| `scripts/linuxcnc.in` | `/tmp/linuxcnc.lock`, `~/linuxcnc_debug.txt`, `~/linuxcnc_print.txt` | Instance suffix (none for instance 0) |

### Network servers and GUI sockets

| Port | Used by | Change |
|---|---|---|
| 5006 | `halrmt` | Default port offset by instance; explicit `-p` taken as given |
| 5007 | `linuxcncrsh` | Same |
| 5008 | `schedrmt` | Same |
| 5690/5691 | ZMQ status/request sockets in `hal_glib.py`, qtvcp `screen_options.py`, `hal_bridge.py` | Offset by instance |

The ZMQ pair matters beyond a failed bind: `hal_glib`'s read socket calls
whatever function name a received message carries, so a second session could
end up listening to the first one's requests.

### Test harness (`scripts/runtests.in`)

* `-j N`: a worker pool pulling from a flock-guarded queue. Each test's report
  is buffered and printed under a lock, so reports do not interleave.
* The shared memory pre-check and post-test cleanup are per instance; a worker
  only ever removes its own segments.
* `LINUXCNC_TEST_TIMEOUT_SCALE` is set to the job count. The waits in
  `lib/python/linuxcnc_util.py` and in the ui-smoke driver stretch by it.
* A test with a `noparallel` file runs alone after the parallel batch.
* Tests that were never run (`-s`, or a worker giving up on a segment it could
  not remove) are listed by name in the summary instead of disappearing from
  the count.
* `-v` forces `-j1`.
* Fixed while restructuring: the old `if ! test_and_remove_shmem; then if [ $? -eq 2 ]`
  could never see the 2, so an unremovable segment never failed the run.

### Tests

* Seven tests connected to `linuxcncrsh` on a literal 5007 and now compute the
  port from the instance: `linuxcncrsh`, `linuxcncrsh-tcp`, `motion/g0`,
  `mdi-queue/*`, and the shared scripts behind `t0/*` and
  `toolchanger/toolno-pocket-differ/*`.
* `tests/mdi-queue/oword-queue-buster` is the only test marked `noparallel`.
  It deliberately races the MDI queue across a tool change and reads `#5400`;
  a neighbouring session changes the outcome. It failed every time at `-j2`
  and passes alone.

## ui-smoke

These were the hardest part. Four separate problems, found in this order:

1. **Signalling the wrong GUI.** `quit-launch.sh` and the offscreen Qt
   screenshot found the GUI with `pgrep -f`. Two tests run touchy, two run
   gmoccapy, two run qtvcp, so `touchy-quit` was SIGTERMing the `touchy`
   test's GUI, and that test then failed with "linuxcnc exited before the
   driver finished". Now narrowed to the process's own instance
   (`tests/ui-smoke/_lib/parallel.sh`). `cleanup-runtime.sh` uses the same
   helper.
2. **Shared config directories.** `touchy`, `touchy-quit` and `touchy-fit`
   all ran `configs/sim/touchy` in place and wrote its interpreter variable
   file together; `axis` ran its config in place too. They now run from a
   private copy (`_lib/mirror-config.sh`), removed afterwards. The qtdragon
   tests already used copies but shared `~/qtdragon.log`; the log now goes
   into the copy.
3. **Timeouts sized for an idle machine.** `launch.sh`, `quit-launch.sh` and
   `drive.py` scale their timeouts by `LINUXCNC_TEST_TIMEOUT_SCALE`.
   The confirm-shot settle loop is deliberately left unscaled: it never
   actually converges for these GUIs and always runs its budget out, so
   scaling it turned a 10 s fallback into two minutes for the same picture.
4. **SIGTERM sent before the GUI had finished starting.** `gmoccapy-quit`
   failed at `-j8` even with a 120 s grace. `drive.py` reports ready as soon
   as the linuxcnc *task* answers over NML, which says nothing about the GUI.
   Instrumented, the SIGTERM landed 2.4 s before gmoccapy installed its real
   handler; the GUI then finished starting, sat idle and repainting, and a
   second SIGTERM killed it at once. `quit-launch.sh` now waits for the GUI
   process to go idle first (`wait_for_gui_ready`: at most 25 CPU ticks per
   second for 3 seconds running). Constructing GUIs measured 44–270 ticks/s,
   idle ones 8–10. 5/5 passes under 16 busy loops, where it had failed about
   half the time.

   Why the first signal is lost is inferred, not proven. gmoccapy's handlers
   never ran at all (a change to make the early handler exit with
   `os._exit()` made no difference and was reverted). That fits CPython
   clearing a signal's pending flag when `signal.signal()` installs a new
   handler for it, which gmoccapy does just before `Gtk.main()`. If that is
   the mechanism, any Python GUI that re-arms its SIGTERM handler at the end
   of startup can drop a SIGTERM that arrives during startup.

`quit-launch.sh` also prints more when a GUI survives SIGTERM: the process
state and signal masks, every thread, every candidate process with its
instance, and a Python stack of all threads (it sends SIGABRT, which the armed
faulthandler turns into a dump in `linuxcnc.err`). All of these came out of
this investigation and are kept because they are what located the cause.

An earlier note said ui-smoke was flaky in parallel because of screenshot
mismatches. That was wrong: `compare_to_reference` never fails a test. The
failures were items 1 and 4 above.

## Not covered / known leftovers

* **RTAI kernel builds** are not shifted; there is one set of kernel modules
  per machine.
* **Hardware.** Instances are isolated from each other's data, not from the
  machine's devices. Two sessions on the same physical hardware or the same
  real config will fight over it. Documented in `linuxcnc(1)`.
* **Dotfiles in `$HOME`** that GUIs write, e.g. `~/.touchy_preferences` and
  `~/.axis_preferences`, are still shared. No test has failed on them.
* **`gmoccapy-prepare.sh` and `qtdragon-prepare.sh` leak their temp configs.**
  They `trap ... EXIT` and then `exec` the launcher, so the trap never runs;
  145 such directories were in `/tmp` after this session. Pre-existing and not
  changed; `mirror-config.sh` avoids it by not using `exec`.
* `configs/sim/touchy/sim.var` and `sim.var.bak` are left over from runs made
  before the tests used private copies. They are untracked and harmless.
* The readiness thresholds were measured on gmoccapy on this machine only.
  If a GUI idles above 25 ticks/s the wait falls through to its (scaled)
  timeout and the test proceeds anyway, so a wrong threshold costs time
  rather than correctness.

## Files

New:
`src/rtapi/rtapi_instance.h`,
`lib/python/linuxcnc_instance.py`,
`tests/ui-smoke/_lib/parallel.sh`,
`tests/ui-smoke/_lib/mirror-config.sh`,
`tests/mdi-queue/oword-queue-buster/noparallel`.

Changed (34 files, +678/−215): the C/C++, scripts, Python and tests listed
above, plus `docs/src/code/writing-tests.adoc` (`-j`, `noparallel`) and
`docs/src/man/man1/linuxcnc.1.adoc.in` (`LINUXCNC_INSTANCE`).
