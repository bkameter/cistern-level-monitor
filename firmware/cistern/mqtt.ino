// SPDX-License-Identifier: GPL-3.0-or-later
// MQTT with Home Assistant discovery

// After loading/saving the settings. The clients keep only pointers, mqttHost/mqttCa must stay global.
void mqttApply() {
  mqtt.setServer(mqttHost.c_str(), mqttPort);
  if (mqttTls == 2) mqttTlsNet.setCACert(mqttCa.c_str());  // also checks the broker name against the certificate
  else mqttTlsNet.setInsecure();
  if (mqttTls) mqtt.setClient(mqttTlsNet);
  else mqtt.setClient(mqttNet);
}

// Connects if needed. On every new connection the sensor is (re)announced to Home Assistant
// (MQTT discovery, retained), so a changed topic or interval reaches HA after saving.
bool mqttConnect() {
  if (mqtt.connected()) return true;
  String id = WiFi.macAddress();  // unique per device: client ID and HA IDs, several cisterns on one broker
  id.replace(":", "");
  id.toLowerCase();
  id = "zisterne_" + id;
  if (!mqtt.connect(id.c_str(), mqttUser.c_str(), mqttPass.c_str())) return false;
  String cfg = "{\"name\":null,\"uniq_id\":\"" + id + "_level\",\"stat_t\":\"" + mqttTopic + "\",\"unit_of_meas\":\"mm\","
               "\"dev_cla\":\"distance\",\"stat_cla\":\"measurement\",\"exp_aft\":" + String(3 * mqttSec) + ",\"ic\":\"mdi:water\","
               "\"dev\":{\"ids\":[\"" + id + "\"],\"name\":\"" + HOSTNAME + "\",\"mdl\":\"cistern-esp32\",\"sw\":\"" VERS "\","
               "\"cu\":\"http://" + WiFi.localIP().toString() + "/config\"}}";
  mqtt.publish(("homeassistant/sensor/" + id + "/config").c_str(), cfg.c_str(), true);
  return true;
}
