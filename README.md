# ESP-DSC-MQTT
ESP8266 based DSC alarm panel monitor.

Continuously sends whatever is received via the DSC Keybus to the MQTT broker. Endpoints are "espdsc/verbose" which is everything, "espdsc/status" which is only status messages 0x05 and 0xA5 and "espdsc/zone" which are zone info.

### Sample Output via MQTT
espdsc {"Time":"08:38:32","EpochSeconds":1514450312,"PanelRaw":"[Panel]  101001010000101110011001101100000011000000000000000000000101011110 (OK)","PanelCommandHex":"a5","PanelMessage":{"PanelDateTime":"2017/12/27 0:24","Armed":0}}

## References

http://www.avrfreaks.net/forum/dsc-keybus-protocol
https://github.com/sjlouw/dsc-alarm-arduino

