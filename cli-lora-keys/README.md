# Configure LoRaWAN keys to the device

This test application provides a control request interface to store the lorawan keys on the flash memory of the device. It is accompanied with a command line utility that provides a user interface for the functionality exposed by the application.

## Pre-requisites

Nodejs 16 or higher (Run `nvm use`)

## Installation

The command line utility is internal to this repository and can be executed directly from the source tree. In this case, the package dependencies need to be installed:
```sh
cd path/to/lorawan-poc-fw/cli-lora-keys
npm install
```

## Usage

Make sure a Particle device running the test application is connected to the computer via USB. Note that the command line utility does not support interacting with multiple devices and expects exactly one device to be connected to the computer.

Send keys to the device using the following command
```sh
node index.js --joinEui 70:B3:D5:7E:D8:00:2C:4D --appKey A4:FE:CB:D4:84:1E:4F:65:67:59:B3:A9:BD:92:64:4F
```

## Verify

Check the logs on user firwmare.
```
0000012756 [app] INFO: Received 24 bytes of data:
70B3D57ED8002C4DA4FECBD4841E4F656759B3A9BD92644F
0000012781 [app] INFO: Writing lorawan keys to DCT...
```

## Reset the device
Reset the device for the keys to take effect