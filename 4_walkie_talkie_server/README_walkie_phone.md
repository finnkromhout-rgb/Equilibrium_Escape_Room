# Walkie talkie – phone (receiver)

`public/walkie_phone_receiver.html` runs on the phone inside the Lock Box. It plays the voice from the control room laptop. It has no microphone of its own.

## Before you start

- The laptop server is running and the laptop page is open (see `README_walkie_laptop.md`).
- You know the laptop's IP address (`ipconfig` → **IPv4 Address**).
- The phone is on the **same Wi-Fi or hotspot** as the laptop.

## Every time you play

1. On the phone, open the browser and go to:
   ```
   http://<laptop IP>:3000/walkie_phone_receiver.html
   ```
   For example `http://172.20.10.3:3000/walkie_phone_receiver.html`.
2. Tap **Setup Connection** once. This is needed because phones only play sound after a tap.
3. The status says *Waiting for control room connection...*. The order does not matter: the phone keeps asking for audio every 2 seconds until the laptop starts.
4. Turn the volume up and set **Auto-Lock to Never** (Settings → Display), so the screen does not switch off in the box.
5. Put the phone in the Lock Box. When the box opens and the laptop starts transmitting, the status says *Audio is playing from control room!*

## Alternative: tunnel link

If the phone cannot reach the laptop's IP address (for example on a school network), use a tunnel. Install [cloudflared](https://developers.cloudflare.com/cloudflare-one/connections/connect-networks/downloads/) on the laptop and run in a second terminal:

```
cloudflared tunnel --url http://localhost:3000
```

It prints an `https://….trycloudflare.com` link. On the phone, open that link with `/walkie_phone_receiver.html` after it.

## Troubleshooting

| Problem | Fix |
|---|---|
| Page does not open | Wrong IP, not on the same network, or the server is not running. Try the tunnel link. |
| *Tap the screen to hear the control room* | Tap anywhere on the screen once. |
| Connected but silent | Volume up; check the phone is not in silent mode. Reload the page and tap **Setup Connection** again. |
| Screen turns off in the box | Set Auto-Lock to Never. |
