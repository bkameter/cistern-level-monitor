# Home Assistant integration via MQTT

[Deutsche Version](home-assistant-de.md)

The cistern publishes the current water level as a plain number in **millimetres**
(e.g. `1234`) to an MQTT topic and announces itself to Home Assistant via
**MQTT discovery**: no YAML needed, the device "Zisterne" appears automatically.

| What | Value |
|---|---|
| Topic | `Wasserstand-Zisterne` (default, adjustable) |
| Payload | level in mm, integer, no unit |
| Interval | 60 s (default, 10–3600 s adjustable) |
| Discovery | `homeassistant/sensor/zisterne_<mac>/config` (retained) |
| Client ID | `zisterne_<mac>` (unique per device) |

Only valid readings are sent. On a measurement error (broken wire, short circuit)
nothing is published, and after 3 publish intervals Home Assistant shows the sensor
as "unavailable".

## 1. In Home Assistant: start the broker and create a login

Home Assistant itself does not include an MQTT broker. The **MQTT integration** is only a
client that connects to one. The broker comes from the **Mosquitto broker** app (called
"add-on" before 2026.2), a one-click install. Apps exist only on Home Assistant OS (e.g.
Home Assistant Green/Yellow, Raspberry Pi). On a Docker/container install, run the broker
separately, e.g. as an `eclipse-mosquitto` container.

1. *Settings → Apps → Install app* → **Mosquitto broker** → *Install*, then *Start*.
2. Create a login for the cistern. This happens **in Home Assistant**, not on the cistern:
   *Settings → Apps → Mosquitto broker → Configuration tab*, add under **Logins**:
   ```yaml
   - username: zisterne
     password: "a-long-random-password"
   ```
   Save, restart the app. Mosquitto also accepts any Home Assistant user, but a
   dedicated login is safer, see [Security](#security).
3. *Settings → Devices & services*: Home Assistant shows the discovered **MQTT**
   integration, click *Add*. Discovery is then enabled by default (prefix `homeassistant`).

## 2. On the cistern: enter the login

No login is created here. The cistern only gets the credentials from step 1.
Open `http://zisterne.local/config` (or the device's IP), tab **Connectivity**, card **MQTT**:

- **Broker**: IP address or hostname of Home Assistant (e.g. `192.168.1.10`)
- **Port**: `1883`
- **User / Password**: the login from step 1
- **Topic**: keep `Wasserstand-Zisterne` or choose your own
- **Publish interval**: e.g. `60` s
- **Test value (mm)**: e.g. `1234` – sent once on saving (see below)

Save. An empty broker field turns MQTT off.

## 3. Test

The **test value** field sends a value right when saving – handy while the sensor
isn't installed yet. The message at the top of the page shows the result:

- "Test value 1234 mm sent to …" – connection works, the device **Zisterne** now
  shows up under *Settings → Devices & services → MQTT* with the sensor
  `sensor.zisterne`.
- "wrong user/password" – check the login from step 1.
- "broker not reachable (code -2)" – wrong address/port, or broker not running.

Enter a few different values one after another to check graphs and automations.
The next real reading replaces the test value; without a sensor it turns
"unavailable" after 3 publish intervals.

Alternatively watch the topic: *Settings → Devices & services → MQTT → Configure →
Listen to a topic*, enter `Wasserstand-Zisterne` (or `#` for everything), *Start listening*.

## 4. Optional: volume and fill percentage

Template sensors derive litres or percent from the level. Adjust the numbers to
your tank:

- **Litres per mm**: base area in m² (= litres per mm of height). Cylinder with
  2 m diameter: 3.14 m² → `3.14`. Rectangular 2 × 1.5 m → `3.0`.
- **Maximum level**: water level at the overflow in mm.

```yaml
template:
  - sensor:
      - name: "Cistern volume"
        unique_id: cistern_volume
        unit_of_measurement: "L"
        device_class: volume_storage
        state_class: measurement
        state: "{{ (states('sensor.zisterne') | float * 3.14) | round(0) }}"
        availability: "{{ has_value('sensor.zisterne') }}"

      - name: "Cistern fill level"
        unique_id: cistern_fill_level
        unit_of_measurement: "%"
        state_class: measurement
        state: "{{ (states('sensor.zisterne') | float / 1800 * 100) | round(0) }}"
        availability: "{{ has_value('sensor.zisterne') }}"
```

For tanks that are not straight-sided (spherical, horizontal cylinder) the
level-to-volume relation is not linear: use a
[Compensation](https://www.home-assistant.io/integrations/compensation/) sensor
with a few measured level/volume pairs instead.

## 5. Optional: low-level alert

```yaml
automation:
  - alias: "Cistern almost empty"
    triggers:
      - trigger: numeric_state
        entity_id: sensor.zisterne
        below: 300
        for: "00:10:00"
    actions:
      - action: notify.notify
        data:
          message: "Cistern level is {{ states('sensor.zisterne') }} mm."
```

## Without discovery (firmware 1.1)

Firmware 1.1 does not send discovery. Define the sensor manually in `configuration.yaml`:

```yaml
mqtt:
  sensor:
    - name: "Zisterne"
      unique_id: zisterne_level
      state_topic: "Wasserstand-Zisterne"
      unit_of_measurement: "mm"
      device_class: distance
      state_class: measurement
      expire_after: 300
```

After updating to a firmware with discovery, remove this block, otherwise the sensor
exists twice.

## Removing the device

The discovery message is retained on the broker. To remove the cistern from Home
Assistant for good: clear the broker field on the cistern, then delete the device in
Home Assistant (*MQTT → device → ⋮ → Delete*). That also removes the retained message.

## Security

- **The device only publishes, it subscribes to nothing.** Nobody can control or
  reconfigure the cistern via MQTT. Discovery doesn't change that.
- **MQTT runs unencrypted** (port 1883, no TLS): login and values travel in plain text
  over the local network. That's why the cistern gets its own Mosquitto login – a Home
  Assistant user's password would also unlock the Home Assistant web UI.
- **Anyone with broker access can fake values or discovery messages.** Keep the broker
  password-protected (Mosquitto app default) and don't expose port 1883 to the internet.
- The discovery message contains the device's MAC and IP – the broker sees those anyway.
- The test value can only be sent from `/config`, i.e. after logging in.

## Troubleshooting

- **No values**: the Telnet log (`telnet zisterne.local`) shows the measurement; send a
  test value and read the message. The device must not be in access point mode.
- **Device doesn't appear**: MQTT integration set up and discovery enabled? Discovery
  prefix must be `homeassistant` (default).
- **Value stays at "unknown"**: wait one publish interval; on a sensor error nothing is sent.
