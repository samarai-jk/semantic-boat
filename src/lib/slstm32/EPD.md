# E-paper displays and render scheduling

The `slstm32::epd` namespace separates portable display behavior from the GPIO,
SPI, timing, and application policy of an individual firmware project. It also
provides a coalescing render queue for responsive user interfaces.

## Architecture

The E-paper support has four layers:

- `Transport` is implemented by the firmware project. It performs command and
  data writes, reset, power control, BUSY reads, delays, and monotonic timing.
- `Panel` is the synchronous panel interface for initialization, complete frame
  display, dimensions, refresh modes, and sleep.
- `AsyncPanel` adds non-blocking transfer and refresh state with an explicit
  safe commit boundary.
- `MonochromeCanvas` and `Font5x7` provide allocation-free drawing on a
  caller-owned one-bit framebuffer.

Reusable controller or panel implementations belong under `slstm32/epd`.
Board-specific SPI handles, GPIO pins, and power circuitry belong in a firmware
transport implementation. Decisions such as when to redraw, which region is
dirty, and when to force a full anti-ghosting refresh belong in a service.

## Framebuffer and coordinates

`MonochromeCanvas` does not allocate memory. The caller must provide at least

```text
((rawWidth + 7) / 8) * rawHeight
```

bytes and keep that storage alive for the lifetime of the canvas and every
active panel transfer.

The canvas may expose a rotated logical orientation. UI services normally
describe dirty rectangles in logical coordinates, then call
`MonochromeCanvas::toRawRegion()` before passing the rectangle to a panel.
Panel drivers may expand horizontal boundaries to meet controller byte-alignment
requirements.

Do not assume that a panel's partial-refresh waveform also permits a partial RAM
upload. Those are separate capabilities. A driver may accept a dirty region as
a scheduling hint and still expand the physical transfer to the complete
framebuffer when its controller requires that.

## Asynchronous update lifecycle

An `AsyncPanel` update follows this state machine:

```text
idle -> transferring -> prepared -> refreshing -> settling -> idle
                                      |
                                      ` physical update has started
```

1. Call `beginDisplay()` with the full framebuffer, refresh mode, and native
   panel region.
2. Call the panel driver's `run()` frequently. It transfers a bounded amount of
   data on each call so services and input processing continue to run.
3. When `updateState()` becomes `prepared`, call `commitDisplay()` to start the
   physical E-paper waveform.
4. Continue calling `run()` while the driver polls BUSY and observes any
   required settling time.

`canCancelDisplay()` is true only before the physical refresh is committed. A
service may call `cancelDisplay()` during `transferring` or `prepared`, then
schedule a replacement containing newer application state. Once the state is
`refreshing`, the controller is driving the panel and the operation must finish.
New input should update the model immediately and remain queued for the next
display operation.

Do not modify framebuffer memory while a transfer is active. Cancel the
transfer first, update or redraw the buffer, and then begin a new transfer.

## `RenderQueue<N>`

`RenderQueue<N>` is a generic, allocation-free dirty-region queue. `N` is the
number of logical regions defined by the consuming service, from 1 through 32.
The queue stores bit sets rather than frame copies:

- `request(id)` marks a region pending.
- `begin()` atomically moves all pending regions into the active set and returns
  its mask.
- `complete()` clears the active set after a successful update.
- `retryActive()` merges the active set back into pending after cancellation or
  failure.
- `pendingMask()` and `activeMask()` expose the corresponding region sets.

A request for a currently active region sets its pending bit again. Therefore,
completion of the older display operation cannot erase newer work for that
region. Requests for unrelated regions remain independent.

The queue deliberately coalesces states: five changes to one value may result
in one display operation, while the application model still receives all five
changes. It guarantees that each dirty region eventually displays its latest
state; it does not guarantee that every intermediate frame is displayed.

Typical service flow:

```cpp
queue.request(changedRegion);

// If an obsolete upload has not yet been committed:
if (queue.active() && panel.canCancelDisplay() && panel.cancelDisplay()) {
    queue.retryActive();
}

// Later, when the panel is idle and input has settled:
const auto regions = queue.begin();
renderLatestModel(framebuffer);
const auto logicalBounds = boundsFor(regions);
const auto rawBounds = canvas.toRawRegion(logicalBounds);
if (!panel.beginDisplay(framebuffer, framebufferSize,
                        RefreshMode::partial, rawBounds)) {
    queue.retryActive();
}
```

If several dirty rectangles cannot be represented by one controller window,
the service may unite them into one bounding rectangle or process them as
separate operations. The queue only tracks identity and lifecycle; mapping
region IDs to rectangles is application policy.

### Waveshare 3.7-inch behavior

`Waveshare3In7` uses the fast A2 waveform for `RefreshMode::partial`, but uploads
the complete 280 x 480 one-bit framebuffer before activation. Windowed RAM
uploads leave controller state outside the window unreliable and can make
persistent black areas alternate between black and white on successive updates.

Logical regions are still useful: the service can discard or preserve work by
region and coalesce obsolete model states. For this panel, they reduce rendering
and scheduling work but not the number of framebuffer bytes transferred over
SPI. The transfer remains chunked and cancellable until `commitDisplay()`.

## Full-refresh policy

Neither `RenderQueue` nor `AsyncPanel` forces periodic full refreshes. Ghosting
behavior, acceptable interruption time, update frequency, temperature, and
panel/controller requirements vary by product. The consuming service should
make this policy explicit and configurable.

Common policies include:

- no periodic full refresh, with a full refresh only at startup or on demand;
- a full refresh after a configurable number of completed partial updates;
- a time-based refresh performed only after the UI has been idle long enough;
- a maintenance refresh triggered by measured image quality or an operating
  mode transition.

When using a count threshold, reserve `0` to disable it and use a counter wide
enough for high-frequency displays. Count only successfully completed partial
updates, reset the count after a successful full refresh, and do not count
cancelled transfers.

## Integration checklist

1. Implement the board-specific `Transport` without blocking longer than the
   underlying bus operation requires.
2. Allocate the framebuffer for the panel's raw orientation.
3. Register the panel as a driver so its `run()` method executes frequently.
4. Define stable logical region IDs in the UI service and instantiate
   `RenderQueue<N>`.
5. Apply every input to the application model before scheduling display work.
6. Cancel only while `canCancelDisplay()` is true.
7. Preserve active regions with `retryActive()` after cancellation or failure.
8. Convert logical dirty bounds to raw panel coordinates.
9. Keep periodic full-refresh policy outside the shared driver and queue.
10. Test cancellation, repeated invalidation of an active region, unrelated
    simultaneous regions, BUSY timeout, coordinate rotation, and counter limits.
