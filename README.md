# BCA152 Smart Fan with Firebase

## Project Overview

An ESP32-S3 smart fan that is controlled from a web page or by voice. The fan is simulated with three LEDs (low, normal, high). Firebase Realtime Database connects the web page and the ESP32.

The original reference project used AWS IoT Core, AWS Lambda, Alexa and relays. Firebase was used instead of the AWS services and Alexa, with the professor's approval, and LEDs replace the relays and fan motor.

## Features

- Web page (`web/`) with email/password login, OFF / LOW / NORMAL / HIGH buttons, and a fan picture that spins faster at each level.
- Voice commands in the browser (Web Speech API): "turn off the fan", "low speed", "normal speed", "high speed". Works in Chrome and Edge.
- The web page listens to Firebase live, so it updates when the value changes from anywhere.
- ESP32 firmware (ESP-IDF) with Wi-Fi auto-reconnect, Firebase email/password sign-in, reading the fan level over HTTPS, token refresh, and re-sign-in when the database rejects the token.
- Database rules that allow only two specific accounts and only the values 0, 33, 66 and 100.

## Learning Objectives

- Use ESP-IDF and PlatformIO to build ESP32-S3 firmware.
- Use FreeRTOS tasks and event groups to coordinate Wi-Fi and cloud communication.
- Authenticate a device and a web app with Firebase Authentication.
- Use the Firebase Realtime Database REST API over HTTPS from an ESP32.
- Protect a database with authenticated access rules and keep secrets out of Git.
- Use the browser Web Speech API for voice control.

## System Architecture

```text
Web page (buttons / microphone)  --writes-->  Firebase Realtime Database  <--reads--  ESP32-S3
        /fan/powerlevel = 0 | 33 | 66 | 100                                 (updates the LEDs)
```

- The web page signs in with the Firebase Web SDK and writes `/fan/powerlevel`.
- The ESP32 signs in with the Firebase Authentication REST API, then reads `/fan/powerlevel` over HTTPS every 2 seconds using the Realtime Database REST API.
- The database rules decide who may read and write.

## FreeRTOS Architecture

- ESP-IDF runs `app_main()`, which sets up NVS, the LEDs and Wi-Fi, then creates one application task, `firebase_task`.
- Wi-Fi and IP events are handled by the ESP-IDF default event loop through `wifi_event_handler`.
- `firebase_task` has an 8192-byte stack and priority 5. It needs the larger stack because HTTPS (TLS) uses a lot of memory.

## Hardware / Simulated Components

| Component | Purpose |
|---|---|
| ESP32-S3 DevKitC-1 | Microcontroller with Wi-Fi |
| 3 LEDs | Simulate LOW, NORMAL and HIGH fan speed |
| Web browser (Chrome or Edge) | Control page and voice input |
| Firebase (Authentication and Realtime Database) | Cloud link between the web page and the ESP32 |

The LED circuit has not been built yet, because no hardware was available during development.

## Pin Configuration

| Fan state | Power level | GPIO |
|---|---:|---:|
| OFF | 0 | all LEDs off |
| LOW | 33 | GPIO 4 |
| NORMAL | 66 | GPIO 5 |
| HIGH | 100 | GPIO 6 |

## Task Design

| Task / handler | Role |
|---|---|
| `app_main` | Initializes NVS, LEDs and Wi-Fi, creates `firebase_task`, then returns |
| `wifi_event_handler` | Connects, reconnects on disconnect, logs the IP address, sets or clears the connected flag |
| `firebase_task` | Waits for Wi-Fi, signs in, polls `/fan/powerlevel` every 2 seconds, refreshes the token, and updates the LEDs |

## Inter-Task Communication

A FreeRTOS event group (`wifi_event_group`) with the bit `WIFI_CONNECTED_BIT` links the Wi-Fi event handler and `firebase_task`. The handler sets the bit when the ESP32 gets an IP address and clears it when Wi-Fi disconnects. `firebase_task` waits on the bit before it makes any network request.

## State Machine

```text
WAIT_FOR_WIFI --> SIGN_IN --> POLL (read /fan/powerlevel every 2 s, update LEDs)
                     ^            |
                     |            +--> token near expiry --> REFRESH TOKEN --(fails)--> SIGN_IN
                     |            +--> database answers 401 ----------------------------> SIGN_IN
                     +-- sign-in fails: wait 10 s and retry
```

If Wi-Fi drops, the task waits for the connected bit again before making the next request.

## Repository Structure

| Path | Purpose |
|---|---|
| `src/main.c` | ESP32 firmware |
| `src/CMakeLists.txt` | Firmware component settings |
| `src/idf_component.yml` | Requests the cJSON component (ESP-IDF 6 no longer includes it) |
| `src/secrets.h.example` | Template for Wi-Fi and Firebase values (real `src/secrets.h` is ignored by Git) |
| `dependencies.lock` | Locked version of the cJSON component |
| `web/index.html`, `web/style.css`, `web/app.js` | Web page |
| `web/firebase-config.example.js` | Template for the Firebase web settings (real `web/firebase-config.js` is ignored by Git) |
| `database.rules.example.json` | Example Realtime Database rules |
| `platformio.ini` | PlatformIO settings (ESP-IDF framework only) |

## Getting Started

1. Create a Firebase project with a Realtime Database, enable Email/Password sign-in, register a Web App, and create two user accounts.
2. Create `/fan/powerlevel` in the database with the value `0`.
3. Copy `database.rules.example.json` into the database Rules tab and replace `UID_ONE` and `UID_TWO` with the User UIDs of your two accounts.
4. Copy `src/secrets.h.example` to `src/secrets.h` and fill in your Wi-Fi name and password, Firebase Web API key, database URL, and the email and password of one allowed account.
5. Copy `web/firebase-config.example.js` to `web/firebase-config.js` and fill in the values from Firebase Project settings, Your apps, Config. The first line must be `export const firebaseConfig = {`.

## Building the Project

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run
```

The first build downloads the cJSON component, so it needs an internet connection.

To run the web page locally:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" -m http.server 8000 --directory web
```

Then open `http://localhost:8000` in Chrome or Edge.

## Running the Wokwi Simulation

Not done. No Wokwi simulation was created for this project.


## Unit Testing

Not done. No automated unit tests have been written yet.

