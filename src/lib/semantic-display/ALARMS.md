# Alert presentation and queue policy

Semantic displays use the Signal K actionable notification levels, with the
least severe `alert` state labelled **Info** for users:

| Level | Presentation |
| --- | --- |
| Info (`alert`) | Full-screen `SYSTEM INFO` message; no exceptional LED or sound |
| Warning (`warn`) | Full-screen warning and large `!`; one short audible cue; yellow LED at 10% duty, blinking at 0.5 Hz |
| Alarm | Full-screen alarm and `!`; red LED at 50% duty, blinking at 1 Hz; continuous rising/falling siren |
| Emergency | Full-screen emergency and `!`; full-white LED blinking at 1.5 Hz; maximum-volume alternating tone |

Audible alarm and emergency output begins only after the EPD reports that the
corresponding full-screen alert has been presented. Device beep-volume and LED
preferences do not suppress safety alarms. The configurable minimum displayed
level may be Info, Warning, Alarm, or Emergency; Emergency cannot be disabled.

## Identity, repetitions, and bounded storage

Each alert has a stable server ID and a monotonically changing occurrence
number. Repeated delivery of the same pair is idempotent. A new occurrence or
severity escalation makes a locally hidden alert visible again.

Remote-A reserves eight fixed records. Visible records are ordered by severity,
then oldest occurrence first. A more severe arrival pre-empts the currently
displayed record. If storage is full, hidden or filtered records are replaced
first. Otherwise a higher-severity arrival may replace the oldest record at the
lowest stored severity. An equal or lower arrival does not evict a more
important record; the screen exposes an overflow count so loss is never silent.
The server remains authoritative and re-announces its active snapshot after a
reconnect.

## Operator actions

The four horizontal buttons are:

- **ACK**: acknowledge globally; the server removes the full-screen alert from
  every display but retains the source condition until it is rectified.
- **SNOOZE**: request temporary global removal; the server re-announces a new
  occurrence if the source condition is still active when the timer expires.
- **HIDE**: hide only on this display and send nothing.
- **SILENT**: keep the visual alert active but request global audible silence.

These messages are asynchronous state announcements. The display never waits
for a response before continuing to process data or other alerts. If a global
ACK or SNOOZE cannot be queued locally, the alert stays visible. If the server
later re-announces that same active occurrence, it becomes visible again;
local-only HIDE remains sticky until a new occurrence or escalation.

This policy follows Signal K's `alert`, `warn`, `alarm`, and `emergency` state
ordering and its centralized notification lifecycle. It also follows the IMO
bridge-alert distinction between acknowledgement, temporary audible silence,
and rectification: an acknowledged underlying condition is not silently treated
as physically resolved. Flash rates stay within the general 0.5–1.5 Hz range
specified by IMO A.1021(26), rather than using that document's special 4 Hz
exception for amber MODU indicators. This implementation is a development
prototype, not a certified marine safety or fire/CO alarm system.

References:

- <https://signalk.org/specification/1.7.0/doc/notifications.html>
- <https://demo.signalk.org/documentation/Developing/REST_APIs/Notifications_API.html>
- <https://wwwcdn.imo.org/localresources/en/KnowledgeCentre/IndexofIMOResolutions/MSCResolutions/MSC.302%2887%29.pdf>
