# Smart Home Hub

An ESP32 + Firebase + Android (Java) home monitoring and control system.

- **ESP32** reads temperature and humidity from a DHT22 every minute and uploads them to Firebase Realtime Database.
- **Android app (Java)** shows the live readings and has an ON/OFF switch that controls a relay (lamp).
- **Cloud Function** sends a push notification when the temperature crosses a threshold (default 30 °C), even when the app is closed.

Inspired by [ESP32N8NButton](https://github.com/ronibandini/ESP32N8NButton) by Roni Bandini (MIT licensed), which sends a button press from an ESP32 to an n8n webhook. This project replaces the button with sensors, replaces n8n with Firebase, and adds a two-way Android app.

```
 DHT22 ──┐                                   ┌── Android app (Java)
         ├── ESP32 ── Wi-Fi ── Firebase ─────┤     live temperature / humidity
 Relay ──┘   (HTTPS REST)    Realtime DB     │     ON/OFF switch  ──► /hub/relay
                                 │            └── FCM push notification
                                 └── Cloud Function (temperature > threshold)
```

## Repository layout

```
SmartHomeHub/
├── esp32/SmartHomeHub/
│   ├── SmartHomeHub.ino       firmware
│   └── secrets.h.example      copy to secrets.h and fill in
├── android/                   Android Studio project (Java)
├── firebase/
│   ├── database.rules.json    security rules
│   ├── firebase.json
│   └── functions/             temperature alert (Node.js)
└── .gitignore
```

## Database structure

| Path | Written by | Meaning |
| --- | --- | --- |
| `/hub/temperature` | ESP32 | °C |
| `/hub/humidity` | ESP32 | % |
| `/hub/updatedAt` | ESP32 | server timestamp (ms) of the last reading |
| `/hub/relay` | Android app | **command**: `true` = lamp ON |
| `/hub/relayState` | ESP32 | **actual** relay state, reported back |
| `/config/tempThreshold` | you (optional) | alert threshold in °C (default 30) |

## Hardware

| Part | Notes |
| --- | --- |
| ESP32 dev board | any ESP32 works |
| DHT22 (AM2302) | more accurate than DHT11 (±0.5 °C vs ±2 °C) |
| 1-channel relay module | opto-isolated, 5 V or 3.3 V compatible |
| 10 kΩ resistor | pull-up between DHT22 data and 3V3 (skip if your DHT22 is on a breakout board that already has one) |

Wiring (default pins, change them in the `.ino` if you like):

```
DHT22  VCC  → 3V3
DHT22  DATA → GPIO 4   (+10 kΩ pull-up to 3V3)
DHT22  GND  → GND

Relay  VCC  → 5V (VIN)  (or 3V3 if the module supports it)
Relay  IN   → GPIO 5
Relay  GND  → GND
```

> ⚠️ **Mains safety.** If the relay switches a 220 V / 110 V lamp, the mains side can kill you. Use an opto-isolated relay module, keep mains wiring inside an insulated enclosure, never touch it while powered, and if you are not experienced with mains voltage, test with a low-voltage LED or a 12 V lamp instead, or ask a qualified electrician.

Most relay modules are **active-LOW**. If the lamp is inverted (ON at boot, OFF when you switch it on), set `RELAY_ACTIVE_LOW false` in the sketch.

## Setup

### 1. Firebase project

1. Go to the [Firebase Console](https://console.firebase.google.com/) and create a project.
2. **Build → Realtime Database → Create database.** Note the URL and the region.
3. **Build → Authentication → Sign-in method**: enable **Email/Password**.
4. **Authentication → Users → Add user**, twice:
   - `esp32-device@...` (for the ESP32)
   - your own email (for the Android app)

   Copy each user's **UID** from the Users table.
5. **Authentication → Settings → User actions**: disable *Enable create (sign-up)* so nobody else can register.
6. Open `firebase/database.rules.json` and replace `REPLACE_WITH_DEVICE_UID` and `REPLACE_WITH_YOUR_APP_UID` with the two UIDs, then paste the rules into **Realtime Database → Rules → Publish** (or deploy them with the CLI).
7. **Project settings → General**: copy the **Web API Key**.

### 2. ESP32

1. Install the **ESP32 board package** in Arduino IDE (or PlatformIO).
2. Library Manager: install **DHT sensor library** (Adafruit) and **Adafruit Unified Sensor**.
3. Copy `secrets.h.example` to `secrets.h` (same folder) and fill in Wi-Fi, API key, database URL, and the device account email/password.
4. Upload `SmartHomeHub.ino` and open the Serial Monitor at **115200 baud**. You should see:

```
[WiFi] Connecting to MyWiFi.... OK
[Auth] Signed in
[DHT] 24.6 C, 48.2 %
```

5. In the Firebase console, `/hub` should now contain `temperature`, `humidity`, `updatedAt`, `relayState`.

### 3. Android app

1. In the Firebase console, **Project settings → Your apps → Add app → Android**. Use the package name `com.example.smarthomehub` (or change `namespace` / `applicationId` in `app/build.gradle` and the Java package to your own).
2. Download **`google-services.json`** and put it in `android/app/`. (It is git-ignored.)
3. Open the `android/` folder in **Android Studio**, let Gradle sync, and run on a phone or emulator (Android 7.0+).
4. Sign in with the app account you created. You should see the live temperature and humidity, and the **Lamp** switch should toggle the relay within about 2 seconds.

Notes:
- Android 13+ asks for notification permission on first launch. Allow it.
- If Android Studio complains about a missing Gradle wrapper, use *File → New → Import Project* on the `android/` folder or run `gradle wrapper` once.

### 4. Push notifications (Cloud Function)

The app subscribes to the FCM topic `alerts` after signing in. The function sends to that topic.

```bash
npm install -g firebase-tools
firebase login
cd firebase
firebase use --add            # pick your project
cd functions && npm install && cd ..
firebase deploy --only functions
```

- Cloud Functions need the **Blaze** (pay-as-you-go) plan; one device stays inside the free quota, but set a budget alert.
- In `functions/index.js`, set `region` to match your Realtime Database region.
- To change the threshold, set `/config/tempThreshold` (a number) in the console. Default is 30.
- The notification fires only when the reading **crosses** the threshold, not on every reading above it.

## Security notes

- Wi-Fi password, API key, and device password live in `secrets.h` only. It is in `.gitignore`. Never commit it, and never commit `google-services.json` to a public repo.
- The rules allow **only** the two UIDs: the device can write sensor values and `relayState`; only the app account can write the `relay` command. Everyone else is denied.
- `setInsecure()` in the sketch skips TLS certificate checking. That is fine for a prototype on your own network; for production pin a root CA with `client.setCACert(...)`.
- The ESP32 polls `/hub/relay` every 2 seconds. If you plan to run on battery or at scale, switch to Firebase's streaming (server-sent events) or MQTT.
- If Wi-Fi drops, the relay keeps its last state and the ESP32 reconnects automatically; sensor readings during the outage are not queued.

## Troubleshooting

| Symptom | Likely cause |
| --- | --- |
| `[Auth] Failed (400)` | wrong device email/password or Email/Password sign-in not enabled |
| `[DB] PATCH ... failed (401)` / `(403)` | rules still contain the placeholders, or wrong UID |
| `[DHT] Read failed` | wrong pin, missing pull-up, or wait 2 s after power-up |
| App shows `Error: Permission denied` | the app account's UID is not the one in the rules |
| Switch toggles but lamp doesn't | check relay wiring, `RELAY_ACTIVE_LOW`, and that `relayState` updates in the console |
| No push notification | Function not deployed / wrong region, app not signed in (topic not subscribed), notification permission denied |

## Ideas to extend

- Add an SSD1306 OLED showing Wi-Fi status and the latest reading.
- Add a PIR motion sensor or MQ-2 gas sensor and a second alert function.
- Add a threshold slider in the app that writes `/config/tempThreshold`.
- Replace polling with Firebase streaming or MQTT.

## License

MIT. Original concept inspired by [ESP32N8NButton](https://github.com/ronibandini/ESP32N8NButton) (MIT) by Roni Bandini.
