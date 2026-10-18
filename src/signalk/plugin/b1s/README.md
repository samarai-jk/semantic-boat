# Semantic Boat B1S

Signal K plugin that brings [B1S](../../../lib/b1s/README.md) devices into
Signal K. Standard PGNs sent by B1S devices are decoded by Signal K itself;
this plugin decodes the B1S messages and publishes their values under
Signal K paths.

- Registers canboat definitions of the B1S messages (Measurement 65280,
  Channel Description 130819), built from the B1S registry
  (`src/lib/b1s/registry.yaml`), so canboat, the Data Fiddler, and
  `@signalk/n2k-debug` decode them too.
- Identifies devices by the unique number of their NAME (the device ID), not
  by address; a device that changes its address keeps its channels.
- Publishes each measurement under `<quantity template with location>`, for
  example `environment.inside.refrigerator.temperature`, or, while the
  channel has no location, under
  `b1s.unassigned.<device ID>.<channel>.<quantity>`. Values are SI as the
  registry defines them; "not available", "error", and "out of range" become
  `null`. Metadata carries the unit, the channel label as display name, and
  the device and channel.
- Asks devices it does not know yet for their address claim and their
  Channel Descriptions (ISO requests, at most every 10 s per device), and
  sends global requests for both 3 s after start.
- The plugin status shows devices, channels, measurements, and the age of the
  last B1S message. `GET /plugins/semantic-boat-b1s/devices` (admin login)
  lists devices and channels with their paths.

A new quantity in the registry needs no plugin change; restart Signal K
after a registry change.

## Setup

1. **B1S provider.** A `canbus-canboatjs` NMEA 2000 connection on the B1S
   interface (on the bench Pi `can1`, see
   [can-bench](../../../tools/can-bench/README.md#raspberry-pi-bench)) with its
   own event names, so that B1S traffic stays apart from NMEA 2000 inside
   Signal K: requests of this plugin go only to B1S, other plugins' NMEA 2000
   output does not reach B1S, and this plugin sees only B1S messages.
   Preferred address 150 (the B1S range for gateways on the Pi). In
   `settings.json`:

   ```json
   {
     "id": "can1", "enabled": true,
     "pipeElements": [{
       "type": "providers/simple",
       "options": {
         "logging": false, "type": "NMEA2000",
         "subOptions": {
           "type": "canbus-canboatjs", "interface": "can1", "uniqueNumber": 600251,
           "preferredAddress": 150,
           "analyzerOutEvent": "b1s-N2KAnalyzerOut",
           "outEvent": "b1s-nmea2000out",
           "jsonOutEvent": "b1s-nmea2000JsonOut"
         }
       }
     }]
   }
   ```

   canboatjs remembers the last claimed address in `canboatjs-data.json`;
   change `lastAddress` there as well (with Signal K stopped) to move an
   existing connection.

2. **Plugin.** Install its dependencies and link it into the Signal K
   configuration directory like the other Semantic Boat plugins:

   ```sh
   (cd src/signalk/plugin/b1s && npm install)
   ln -s ../../../semantic-boat/src/signalk/plugin/b1s ~/.signalk/node_modules/@semantic-boat/b1s
   ```

   and add `"@semantic-boat/b1s": "file:../semantic-boat/src/signalk/plugin/b1s"`
   to the dependencies in `~/.signalk/package.json`. Enable **Semantic Boat
   B1S** in the plugin configuration (the defaults match the provider above)
   and restart Signal K.

The plugin reads the registry from the repository (`registryPath` overrides
it).

## Tests

```sh
cd src/signalk/plugin/b1s && npm test
```

The tests decode frames recorded from LT1 with canboat and check paths,
metadata, special values, requests, and address changes.
