const {onOff, lightBrightness, colorHS} = require('zigbee-herdsman-converters/lib/exposes');
const fz = require('zigbee-herdsman-converters/converters/fromZigbee');
const tz = require('zigbee-herdsman-converters/converters/toZigbee');
const e = require('zigbee-herdsman-converters/lib/exposes');

const definition = {
    zigbeeModel: ['night-light'],
    model: 'night-light',
    vendor: 'Espressif',
    description: 'ESP32-C6 WS2812B Night Light',
    fromZigbee: [fz.on_off, fz.brightness, fz.color_colortemp_and_level],
    toZigbee: [tz.on_off, tz.light_brightness, tz.light_color],
    exposes: [e.light().withBrightness().withColor(['hs'])],
};

module.exports = definition;
