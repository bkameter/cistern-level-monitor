# Home-Assistant-Einbindung per MQTT

[English version](home-assistant.md)

Die Zisterne sendet den aktuellen Wasserstand als reine Zahl in **Millimetern**
(z. B. `1234`) an ein MQTT-Topic und meldet sich per **MQTT-Discovery** selbst bei
Home Assistant an: Kein YAML nötig, das Gerät „Zisterne“ erscheint automatisch.

| Was | Wert |
|---|---|
| Topic | `Wasserstand-Zisterne` (Standard, änderbar) |
| Payload | Füllstand in mm, Ganzzahl, ohne Einheit |
| Intervall | 60 s (Standard, 10–3600 s einstellbar) |
| Discovery | `homeassistant/sensor/zisterne_<mac>/config` (retained) |
| Client-ID | `zisterne_<mac>` (pro Gerät eindeutig) |

Es werden nur gültige Messwerte gesendet. Bei einem Messfehler (Kabelbruch,
Kurzschluss) wird nichts veröffentlicht, nach 3 Sendeintervallen zeigt Home Assistant
den Sensor als „nicht verfügbar“.

## 1. In Home Assistant: Broker starten und Login anlegen

Home Assistant selbst enthält keinen MQTT-Broker. Die **MQTT-Integration** ist nur ein
Client, der sich mit einem Broker verbindet. Den Broker liefert die App (bis 2026.1
„Add-on“) **Mosquitto broker**, die man mit einem Klick installiert. Apps gibt es
nur bei Home Assistant OS (z. B. Home Assistant Green/Yellow, Raspberry Pi). Bei einer
Docker-/Container-Installation den Broker separat betreiben, z. B. als
`eclipse-mosquitto`-Container.

1. *Einstellungen → Apps → App installieren* → **Mosquitto broker** → *Installieren*,
   dann *Starten*.
2. Login für die Zisterne anlegen. Das geht **in Home Assistant**, nicht in der Zisterne:
   *Einstellungen → Apps → Mosquitto broker → Reiter Konfiguration*, unter **Logins**
   eintragen:
   ```yaml
   - username: zisterne
     password: "ein-langes-zufaelliges-passwort"
   ```
   Speichern, App neu starten. Alternativ akzeptiert Mosquitto jeden Home-Assistant-
   Benutzer. Ein eigenes Login ist aber sicherer, siehe [Sicherheit](#sicherheit).
3. *Einstellungen → Geräte & Dienste*: Home Assistant meldet die gefundene
   **MQTT**-Integration, *Hinzufügen* klicken. Discovery ist damit standardmäßig aktiv
   (Präfix `homeassistant`).

## 2. In der Zisterne: Login eintragen

Hier wird kein Login angelegt. Die Zisterne bekommt nur die Zugangsdaten aus Schritt 1.
`http://zisterne.local/config` (oder die IP des Geräts) öffnen, Reiter **Verbindung**,
Karte **MQTT**:

- **Broker**: IP-Adresse oder Hostname von Home Assistant (z. B. `192.168.1.10`)
- **Port**: `1883`
- **Benutzer / Passwort**: das Login aus Schritt 1
- **Topic**: `Wasserstand-Zisterne` lassen oder eigenes wählen
- **Sendeintervall**: z. B. `60` s
- **Testwert (mm)**: z. B. `1234` – wird beim Speichern einmal gesendet (siehe unten)

Speichern. Ein leeres Broker-Feld schaltet MQTT ab.

## 3. Testen

Das Feld **Testwert** sendet beim Speichern sofort einen Wert – praktisch, solange der
Sensor noch nicht eingebaut ist. Die Meldung oben auf der Seite zeigt das Ergebnis:

- „Testwert 1234 mm gesendet an …“ – Verbindung steht, das Gerät **Zisterne** erscheint
  jetzt unter *Einstellungen → Geräte & Dienste → MQTT* mit dem Sensor `sensor.zisterne`.
- „Benutzer/Passwort falsch“ – Login aus Schritt 1 prüfen.
- „Broker nicht erreichbar (Code -2)“ – Adresse/Port falsch oder Broker läuft nicht.

Mehrere verschiedene Werte nacheinander eingeben, um Verlauf und Automationen zu prüfen.
Der nächste echte Messwert ersetzt den Testwert; ohne Sensor wird er nach 3
Sendeintervallen „nicht verfügbar“.

Alternativ das Topic mitlesen: *Einstellungen → Geräte & Dienste → MQTT → Konfigurieren →
Auf ein Topic hören*, `Wasserstand-Zisterne` (oder `#` für alles) eintragen,
*Anfangen zuzuhören*.

## 4. Optional: Volumen und Füllgrad in Prozent

Template-Sensoren rechnen den Füllstand in Liter oder Prozent um. Die Zahlen an die
eigene Zisterne anpassen:

- **Liter pro mm**: Grundfläche in m² (= Liter pro mm Höhe). Zylinder mit 2 m
  Durchmesser: 3,14 m² → `3.14`. Rechteckig 2 × 1,5 m → `3.0`.
- **Maximaler Füllstand**: Wasserstand am Überlauf in mm.

```yaml
template:
  - sensor:
      - name: "Zisterne Volumen"
        unique_id: zisterne_volumen
        unit_of_measurement: "L"
        device_class: volume_storage
        state_class: measurement
        state: "{{ (states('sensor.zisterne') | float * 3.14) | round(0) }}"
        availability: "{{ has_value('sensor.zisterne') }}"

      - name: "Zisterne Füllgrad"
        unique_id: zisterne_fuellgrad
        unit_of_measurement: "%"
        state_class: measurement
        state: "{{ (states('sensor.zisterne') | float / 1800 * 100) | round(0) }}"
        availability: "{{ has_value('sensor.zisterne') }}"
```

Bei Behältern mit nicht senkrechten Wänden (Kugel, liegender Zylinder) ist der
Zusammenhang zwischen Füllstand und Volumen nicht linear: dann besser einen
[Compensation](https://www.home-assistant.io/integrations/compensation/)-Sensor mit
einigen gemessenen Füllstand/Volumen-Paaren verwenden.

## 5. Optional: Warnung bei niedrigem Füllstand

```yaml
automation:
  - alias: "Zisterne fast leer"
    triggers:
      - trigger: numeric_state
        entity_id: sensor.zisterne
        below: 300
        for: "00:10:00"
    actions:
      - action: notify.notify
        data:
          message: "Zisterne: Füllstand {{ states('sensor.zisterne') }} mm."
```

## Ohne Discovery (Firmware 1.1)

Firmware 1.1 sendet keine Discovery. Dann den Sensor von Hand in der
`configuration.yaml` anlegen:

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

Nach dem Update auf eine Firmware mit Discovery diesen Block entfernen, sonst gibt es
den Sensor doppelt.

## Gerät entfernen

Die Discovery-Nachricht bleibt am Broker gespeichert (retained). Um die Zisterne
dauerhaft aus Home Assistant zu entfernen: in der Zisterne das Broker-Feld leeren, dann
in Home Assistant das Gerät löschen (*MQTT → Gerät → ⋮ → Löschen*). Das entfernt auch
die gespeicherte Nachricht.

## Sicherheit

- **Das Gerät sendet nur, es abonniert nichts.** Über MQTT kann niemand die Zisterne
  steuern oder umkonfigurieren. Daran ändert Discovery nichts.
- **MQTT läuft unverschlüsselt** (Port 1883, kein TLS): Login und Werte gehen im Klartext
  durchs lokale Netz. Deshalb bekommt die Zisterne ein eigenes Mosquitto-Login – das
  Passwort eines Home-Assistant-Benutzers würde auch die Home-Assistant-Oberfläche öffnen.
- **Wer Zugang zum Broker hat, kann Werte oder Discovery-Nachrichten fälschen.** Broker
  mit Passwort betreiben (Standard bei der Mosquitto-App) und Port 1883 nicht ins
  Internet freigeben.
- Die Discovery-Nachricht enthält MAC und IP des Geräts – die sieht der Broker ohnehin.
- Der Testwert lässt sich nur über `/config` senden, also nach dem Login.

## Fehlersuche

- **Keine Werte**: Das Telnet-Log (`telnet zisterne.local`) zeigt die Messung; einen
  Testwert senden und die Meldung lesen. Das Gerät darf nicht im Access-Point-Modus sein.
- **Gerät erscheint nicht**: MQTT-Integration eingerichtet und Discovery aktiv?
  Discovery-Präfix muss `homeassistant` sein (Standard).
- **Wert bleibt „unbekannt“**: ein Sendeintervall abwarten; bei Sensorfehler wird nichts
  gesendet.
