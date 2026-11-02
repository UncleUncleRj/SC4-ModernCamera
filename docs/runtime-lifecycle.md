# Runtime Lifecycle and Logging

This document records the plugin lifetime rules that protect SimCity 4's main
thread while retaining useful diagnostic output.

## Startup

`OnStart()` opens the current log and rotates the previous log before the
background writer exists. Startup records are therefore written synchronously.

The director then registers itself with `cIGZFrameWork` for lifecycle callbacks.
Only after that registration succeeds does the logger start its worker thread.
If either registration or worker creation fails, the plugin continues with
synchronous logging rather than creating an unmanaged thread.

`OnStart()` also adds the camera frame service to the framework tick list. If that
fails, a repeating Win32 timer runs the same frame while a city is loaded.

## Camera frame

The tick service runs at priority 2,000,000. The framework ticks services from
high to low, so it runs after the message server (5,000,000) and before the draw
system (1,500,000). A frame applies held keys and accumulated mouse rotation, then
completes the camera updates requested since the previous frame. Held Q/E rotation
uses the measured frame time, capped at 0.25 seconds. With no city loaded, a frame
only records the time.

`PreAppShutdown()` removes the service from the tick list before stopping the
logger.

SC4's message server and tick loop catch nothing. An exception escaping a tick
skips later services, so the tick, director messages, canvas filter, and low-level
keyboard hook each stop and log C++ exceptions at their boundary.

## Asynchronous logging

Once the worker starts, the game thread creates a timestamped record and tries
to place it in a bounded queue. It never waits for the queue lock or for disk
I/O. If the queue is busy or full, the record is dropped and the worker writes a
later warning that reports the number of dropped records.

The worker owns timestamp formatting, file writes, periodic flushing, and log
closure. It must not call SimCity 4, GZCOM, UI, camera, or other game-state
APIs.

## Release notes download

`ReleaseNotesDownloader` runs each download on an owned worker. The worker only
uses WinHTTP and its own state; the game thread polls it from a timer, updates
the window, and writes the log. City shutdown cancels the timer but leaves a
running download to finish, and its result is kept for the session.

`PreAppShutdown()` and `AbortiveQuit()` both join the worker. WinHTTP does not
permit closing a synchronous request from another thread, so the wait is bounded
by the network timeouts (3 to 5 seconds per step) and a stop flag checked between
reads. It can only be felt when quitting during a stalled download.

## Shutdown

`PreAppShutdown()` is the normal teardown point. It is available even when the
player exits without loading a city. The callback writes its final lifecycle
record, asks the logger to drain queued records, joins the worker, and closes the
file. No later framework callback writes to the log.

`AbortiveQuit()` is different: it only signals the worker to stop and does not
wait for it. The plugin's unload check remains false while the worker could
still execute plugin code, preventing the DLL from being unloaded underneath the
thread. A process-lifetime logger instance avoids static-destruction races during
that abortive path.

## Log files

At startup, `SC4-ModernCamera.log` is replaced with a new current log and the
previous current log is retained as `SC4-ModernCamera.last`. Both files live in
the plugin data folder beside the DLL.

## Manual check

After a normal launch and exit, the current log should contain these records in
order:

1. `Registered framework lifecycle hook.`
2. `Starting asynchronous logger.`
3. `Asynchronous logger started.`
4. `Framework lifecycle: PreAppShutdown. Stopping asynchronous logger.`

The final record confirms that normal teardown ran before the writer closed the
file. It is normal for the log to contain no post-shutdown lifecycle records.
