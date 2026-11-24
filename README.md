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
