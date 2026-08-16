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

## Asynchronous logging

Once the worker starts, the game thread creates a timestamped record and tries
to place it in a bounded queue. It never waits for the queue lock or for disk
I/O. If the queue is busy or full, the record is dropped and the worker writes a
later warning that reports the number of dropped records.

The worker owns timestamp formatting, file writes, periodic flushing, and log
closure. It must not call SimCity 4, GZCOM, UI, camera, or other game-state
APIs.

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
