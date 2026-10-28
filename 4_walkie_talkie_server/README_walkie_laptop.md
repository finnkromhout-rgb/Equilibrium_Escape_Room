# Walkie talkie – laptop (sender)

`public/walkie_laptop_sender.html` sends the laptop microphone one way to the phone inside the Lock Box. It starts by itself when the players enter the correct morse code (the Main Game Screen sends `phone_open` over OOCSI).

## What you need

- [Node.js](https://nodejs.org) (LTS version) installed on the laptop
- The laptop and the phone on the **same Wi-Fi**, for example a phone hotspot. On Windows, set this network to **Private**.
- Internet on that network (the pages connect to the OOCSI server `oocsi.id.tue.nl`)
- Chrome or Edge

## First time only: install

1. Open this folder (`4_walkie_talkie_server`) in File Explorer.
2. Click the address bar, type `cmd` and press Enter. A terminal opens in this folder.
3. Type `npm install` and press Enter. This creates the `node_modules` folder.

## Every time you play

1. Open a terminal in this folder (see above) and start the server:
   ```
   node server.js
   ```
   You should see `Server established!`. Keep this window open during the whole game.
2. In Chrome, open: **http://localhost:3000/walkie_laptop_sender.html**
3. Click **Arm Microphone** and allow microphone access. The status says *Microphone armed*.
4. Leave this tab open. When the code is entered, transmission starts by itself. To test without playing, click **Start Transmission**.
5. When the phone is connected, the status says *Communication is online!* and the laptop says it out loud.

The laptop must use `localhost`: browsers only allow the microphone on `localhost` or on an `https` link.

## IP address for the phone

The phone needs the laptop's IP address. In a terminal, type `ipconfig` and look for **IPv4 Address** under your Wi-Fi adapter, for example `172.20.10.3`.

## Troubleshooting

| Problem | Fix |
|---|---|
| Page does not load | The server is not running: start `node server.js` again. |
| *OOCSI: script did not load* | No internet on this network. |
| Microphone blocked | Click the lock icon next to the address and allow the microphone. |
| *Communication is online* but the phone hears nothing | Turn the phone volume up and tap its screen once. Make sure both devices are on the same hotspot. |
| Phone cannot open its page | Windows Firewall: allow Node.js on **private** networks, and check that the network is set to Private. |
