# Remote-A firmware

## Device setup

Hold the Down button (`BTN1`) for one second to open **Setup**. A short press
continues to move to the next display section. Setup contains:

- **Settings** for device-local preferences;
- **Apps** for display applications/configurations offered by the server.

Selection dialogs use Up and Down to move, Previous or Action 1 to go back,
and Next or Action 2 to select.

The initial settings are:

- beep volume: **Min**;
- LED mode: **Normal**.
- minimum shown alert level: **Info**.

Beep volume applies to button, sleep, and wake cues. The passive buzzer is
driven from TIM15 PWM. Off disables ordinary tones; Min, Medium, and Max use
2%, 10%, and 50% output duty respectively. Perceived loudness is hardware and
frequency dependent, so these deliberately broad steps should be evaluated on
the assembled device. Once startup has completed successfully, the device plays
a short welcome beep at the configured ordinary volume.

Normal LED mode enables the green communication/activity indicator and drives
the RGB status LED at 1% duty. Subdued mode disables the activity indicator
(including button flashes) and drives the RGB status LED at 0.1% duty. Off mode
disables both normal indicators. Error/alarm state overrides all LED modes and
enables the red status LED at normal brightness. After an error or alarm dialog
has finished rendering, the device plays three short attention beeps at maximum
buzzer output regardless of the configured ordinary beep volume, so both can
override silent mode.
The communication LED remains an activity indicator and is not used for
alarms. An automatic day/night LED mode is intentionally deferred until server
data can provide the required ambient state.

For development, Action 1 is temporarily labelled **Error** and opens a test
error dialog. Action 2 remains the full-display **Refresh** action.

## Signal K alerts

Incoming Info, Warning, Alarm, and Emergency notifications use a dedicated
full-screen presentation which pre-empts pages, setup dialogs, and paused normal
rendering. The implementation retains up to eight identified notifications and
deduplicates repeated protocol frames. Alarm behavior and the four horizontal
button actions are detailed in
[semantic-display/ALARMS.md](../../lib/semantic-display/ALARMS.md).

Under **Settings**, **Show alarm levels** selects the minimum level shown by
this device. Choosing **Emergency only** suppresses Info, Warning, and Alarm;
Emergency is always enabled. This preference is stored with the other device
settings in EEPROM.

## EEPROM layout

The AT24C256C uses 7-bit I2C address `0x52`. The compiled display-application
store occupies two large alternating slots. The final 256 bytes are reserved
for device setup. The settings store currently uses the first 128 bytes of that
reservation as two alternating 64-byte records; the remaining 128 bytes stay
reserved for future settings data.

Each settings record contains a version, generation, payload CRC, and commit
marker. The marker is written last. Startup selects the newest valid generation
and falls back to defaults if neither record is valid. Updating a preference
therefore leaves the previous generation recoverable if power is interrupted.

The protocol and server continue to call downloadable display applications
"configurations" internally. **Apps** is only the clearer user-facing name.
