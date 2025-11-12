#!/usr/bin/env node
const parseArgs = require('minimist');
const { getDevices } = require('particle-usb')

const REQUEST_TYPE = 10; // ctrl_request_type::CTRL_REQUEST_APP_CUSTOM

async function run() {
    let ok = true;
    try {
        const { joinEui, appKey } = getKeys();

        const { sanitizedJoinEui, sanitizedappKey } = sanitizeKeys(joinEui, appKey);

        validateKeys(sanitizedJoinEui, sanitizedappKey);

        const reqMsg = createReqMsg(sanitizedJoinEui, sanitizedappKey);
        
        const dev = await openDevice();

        if (!dev) {
            throw new Error("Failed to open device");
        }

        await writeToDevice(dev, reqMsg);

    } catch (err) {
        console.error(err);
        ok = false;
    }
    process.exit(ok ? 0 : 1);
}

function getKeys() {
    console.log("Getting keys...");
    const args = parseArgs(process.argv.slice(2));
    const { joinEui, appKey } = args;

    if (!joinEui || !appKey) {
        throw new Error("You must provide values for joinEui and appKey.");
    }
    console.log(`joinEui: ${joinEui}`);
    console.log(`appKey: ${appKey}`);
    return { joinEui, appKey };
};

// TODO: make this for any number of keys
function sanitizeKeys(joinEui, appKey) {
    return { 
        sanitizedJoinEui: joinEui.replace(/:/g, ''),
        sanitizedappKey: appKey.replace(/:/g, '')
    };
}

function validateKeys(joinEui, appKey) {
    console.log("Validating keys...");

    validateLength(joinEui, 16, "joinEui");
    validateHex(joinEui, "joinEui");

    validateLength(appKey, 32, "appKey");
    validateHex(appKey, "appKey");
}

function validateLength(value, length, name) {
    if (value.length !== length) {
        throw new Error(`${name} must be ${length} characters long.`);
    }
}

function validateHex(value, name) {
    if (!/^[0-9a-fA-F]+$/.test(value)) {
        throw new Error(`${name} must be a hex string.`);
    }
}

function createReqMsg(joinEui, appKey) {
    console.log("Creating request message...");
    const joinEuiBytes = Buffer.from(joinEui, 'hex');
    const appKeyBytes = Buffer.from(appKey, 'hex');

    const reqMsg = Buffer.concat([joinEuiBytes, appKeyBytes]);
    
    console.log("Req Msg: ", reqMsg.toString('hex'));
    
    return reqMsg;
}

async function openDevice() {
    console.log("Opening device...");
    const devs = await getDevices();
    if (!devs.length) {
        throw new Error('No devices found');
    }
    if (devs.length !== 1) {
        throw new Error('Multiple devices found');
    }
    const dev = devs[0];
    await dev.open();
    return dev;
}

async function writeToDevice(dev, reqMsg) {
    console.log("Writing to device...");
    return await sendRequest(dev, reqMsg);
}

async function sendRequest(dev, data) {
    console.log("Sending request...");
    let resp;
    try {
      resp = await dev.sendControlRequest(REQUEST_TYPE, data, { timeout: 10000 });
    } catch (err) {
      throw new Error('Failed to send control request', { cause: err });
    }
    if (resp.result < 0) {
      throw new Error("Device-OS error code: ", resp.result);
    }
    return resp.data || null;
}

run();
