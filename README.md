# Blueprint – Advanced – LoRaWAN on Muon

**Repository:** `blueprint-muon-lorawan`  
**Difficulty:** Advanced  
**Estimated Time:** 30–60 minutes  
**Hardware Needed:** Muon board, M-SoM M.2 module, USB cable  
**Particle Features:** `LoRa`, `Muon`, `M-SoM`, `Protobuf`, `CLI`

---

## Overview
This blueprint demonstrates how to initialize, join, and send messages using the **LoRa radio on the Particle Muon**.  
It uses the **M-SoM M.2 module** as host and provides an example workflow to connect to LoRaWAN networks such as **The Things Industries** or **Helium**.

---

## Tools & Materials
- Particle **Muon board**
- **M-SoM** M.2 host module
- **USB-C cable**
- [Particle Workbench](https://docs.particle.io/workbench/) or [Particle CLI](https://docs.particle.io/tutorials/developer-tools/cli/)
- LoRaWAN network credentials (DevEUI, JoinEUI, AppKey)

---

:::info Note
This Blueprint is only supported on Device OS v6.0.0 and above.
:::

## Steps

### 1. Hardware Setup
Insert the M-SoM module into the Muon M.2 socket and connect via USB.

### 2. Build and Flash the App
Compile and flash using Workbench or the CLI.

### 3. Get the Device ID
Run:
```bash
particle usb list
```
The LoRaWAN **DevEUI** is derived from the device ID as:
```
94:94:4A:00:00:aa:bb:cc
```
where `aabbcc` are the last 3 bytes of your device ID.

### 4. Register Device on a LoRaWAN Network
Add your device to **The Things Industries** or **Helium**.  
Note the JoinEUI (application EUI). For the AppKey, generate a random value.

### 5. Send Keys to the Device
Use the CLI utility in `cli-lora-keys` to send JoinEUI and AppKey to your device:
```bash
node index.js --joinEui 70:B3:D5:7E:D8:00:2C:4D --appKey A4:FE:CB:D4:84:1E:4F:65:67:59:B3:A9:BD:92:64:4F
```

### 6. Connect and Verify
Open the serial monitor to observe join and uplink messages:
```bash
particle serial monitor --follow
```
If a gateway is in range, your device should connect and send uplinks!

---

## Pre-Compiling Protobuf Definitions
Ensure the following submodules are checked out and build the protobuf definitions:
```bash
git checkout main
git pull
git submodule update --init --recursive
./lib/device-os-protobuf/build.sh
```

To add a new message:
- Switch to the `constrained/sc-126020` branch in `device-os-protobuf`
- Add your new message to the `.proto` file
- Update the submodule in this repo
- Re-run the build script

---

## Workbench Build & Flash
Import this project into Workbench and compile for the **msom** target (Device OS 6.x).

---

## Cloud Build & Flash
```bash
particle compile msom . --target 6.3.4 --saveTo msom-lora@6.3.4.bin.zip
particle usb dfu
particle flash --local msom-lora@6.3.4.bin.zip
```

---

## Known Issues
1. Protobuf integration is not finalized.  
2. No AT pass-through to the LoRa module; use a dedicated app or modify this one if needed.

