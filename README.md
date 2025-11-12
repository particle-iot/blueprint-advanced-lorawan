## LoRaWAN blueprint for Muon

A simple blueprint app to init, join and send messages with the LoRa radio on the Muon, using a M-SoM M.2 module as host. 

## Getting started

1. Set up hardware: M-SoM in a Muon.
2. Build and flash the app with Workbench.
3. Get the device ID of your M-SoM with `particle usb list`.
   * The LoRaWAN DevEUI is `94:94:4A:00:00:aa:bb:cc` where `aabbcc` are the last 3 bytes of the device ID.
5. Add your device to The Things Industries or Helium, noting down the JoinEUI of the application. For the AppKey, generate a random value.
6. Sends the JoinEUI and AppKey to your device with the [cli-lora-keys](cli-lora-keys/README.md)
   * `node index.js --joinEui 70:B3:D5:7E:D8:00:2C:4D --appKey A4:FE:CB:D4:84:1E:4F:65:67:59:B3:A9:BD:92:64:4F`
7. Open the serial port `particle serial monitor --follow` and if there's a gateway in range, it should connect!

## Pre-Compiling Protobuf definitions

Ensure that the `device-os-protobuf` and `nanopb` submodules are checked out and run the build script:

```sh
git checkout main
git pull
git submodule update --init --recursive
./lib/device-os-protobuf/build.sh
```

To add a new message, check out the `constrained/sc-126020` branch in `device-os-protobuf`, add the message to the `.proto` file, update the submodule in this repo, and run the build script again.

## Workbench Build & Flash

Import project into workbench and compile with `msom` target and Device OS 6x.

## Cloud Build & Flash

`$ particle compile msom . --target 6.3.4 --saveTo msom-lora@6.3.4.bin.zip; particle usb dfu; particle flash --local msom-lora@6.3.4.bin.zip`

## Known Issues

1. Protobuf not integrated yet
2. No AT Pass-through directly to LoRa module, please use another dedicated app for that or modify this application
