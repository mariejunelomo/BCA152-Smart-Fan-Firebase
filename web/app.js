import { initializeApp } from "https://www.gstatic.com/firebasejs/11.0.2/firebase-app.js";
import {
  getAuth,
  signInWithEmailAndPassword,
  signOut,
  onAuthStateChanged
} from "https://www.gstatic.com/firebasejs/11.0.2/firebase-auth.js";
import {
  getDatabase,
  ref,
  set,
  onValue
} from "https://www.gstatic.com/firebasejs/11.0.2/firebase-database.js";

import { firebaseConfig } from "./firebase-config.js";

const app = initializeApp(firebaseConfig);
const auth = getAuth(app);
const db = getDatabase(app);
const powerRef = ref(db, "fan/powerlevel");

const LEVEL_NAMES = { 0: "OFF", 33: "LOW", 66: "NORMAL", 100: "HIGH" };

const loginSection = document.getElementById("login-section");
const controlSection = document.getElementById("control-section");
const loginForm = document.getElementById("login-form");
const emailInput = document.getElementById("email");
const passwordInput = document.getElementById("password");
const loginError = document.getElementById("login-error");
const controlError = document.getElementById("control-error");
const fan = document.getElementById("fan");
const statusText = document.getElementById("status");
const speedButtons = document.querySelectorAll(".speed-btn");
const logoutBtn = document.getElementById("logout-btn");

let stopListening = null;

/* Updates the fan picture, the highlighted button and the status text. */
function showLevel(level) {
  const known = level in LEVEL_NAMES;

  fan.classList.remove("speed-0", "speed-33", "speed-66", "speed-100");
  if (known) {
    fan.classList.add("speed-" + level);
  }

  speedButtons.forEach((button) => {
    button.classList.toggle("active", Number(button.dataset.level) === level);
  });

  statusText.textContent = known
    ? "Fan: " + LEVEL_NAMES[level] + " (" + level + "%)"
    : "Fan: unknown value";
}

/* Writes the chosen level to Firebase. The fan updates when Firebase confirms. */
async function setLevel(level) {
  controlError.textContent = "";
  try {
    await set(powerRef, level);
  } catch (error) {
    console.error("Write failed:", error.code);
    controlError.textContent = "Could not change the fan. Please try again.";
  }
}

function friendlyAuthMessage(code) {
  if (code === "auth/invalid-credential" ||
      code === "auth/wrong-password" ||
      code === "auth/user-not-found" ||
      code === "auth/invalid-email") {
    return "Wrong email or password.";
  }
  if (code === "auth/too-many-requests") {
    return "Too many attempts. Please wait a moment and try again.";
  }
  if (code === "auth/network-request-failed") {
    return "Network problem. Check your internet connection.";
  }
  return "Could not sign in. Please try again.";
}

loginForm.addEventListener("submit", async (event) => {
  event.preventDefault();
  loginError.textContent = "";

  try {
    await signInWithEmailAndPassword(
      auth,
      emailInput.value.trim(),
      passwordInput.value
    );
    passwordInput.value = "";
  } catch (error) {
    console.error("Sign-in failed:", error.code);
    loginError.textContent = friendlyAuthMessage(error.code);
  }
});

logoutBtn.addEventListener("click", () => {
  signOut(auth);
});

speedButtons.forEach((button) => {
  button.addEventListener("click", () => {
    setLevel(Number(button.dataset.level));
  });
});

/* ---------- Voice control (Web Speech API) ---------- */
const micBtn = document.getElementById("mic-btn");
const voiceFeedback = document.getElementById("voice-feedback");

const SpeechRecognition =
  window.SpeechRecognition || window.webkitSpeechRecognition;

/* Turns spoken text into a power level, or null if it is not a clear command. */
function parseCommand(text) {
  const words = text
    .toLowerCase()
    .replace(/[^a-z\s]/g, " ")
    .split(/\s+/)
    .filter(Boolean);

  const found = new Set();

  if (words.includes("off") || words.includes("stop")) found.add(0);
  if (words.includes("low")) found.add(33);
  if (words.includes("normal") || words.includes("medium")) found.add(66);
  if (words.includes("high")) found.add(100);

  // Only accept the command if exactly one speed was mentioned.
  return found.size === 1 ? [...found][0] : null;
}

if (!SpeechRecognition) {
  micBtn.disabled = true;
  voiceFeedback.textContent =
    "Voice control is not supported in this browser. Try Chrome or Edge.";
} else {
  const recognition = new SpeechRecognition();
  recognition.lang = "en-US";
  recognition.continuous = false;
  recognition.interimResults = false;

  micBtn.addEventListener("click", () => {
    voiceFeedback.textContent = "";
    try {
      recognition.start();
    } catch (error) {
      console.error("Could not start listening:", error.name);
    }
  });

  recognition.addEventListener("start", () => {
    micBtn.classList.add("listening");
    micBtn.textContent = "🎤 Listening...";
    voiceFeedback.textContent = "Say: off, low, normal, or high.";
  });

  recognition.addEventListener("result", (event) => {
    const heard = event.results[0][0].transcript.trim();
    const level = parseCommand(heard);

    if (level === null) {
      voiceFeedback.textContent =
        'Heard: "' + heard + '" - not a fan command. Try "high speed".';
    } else {
      voiceFeedback.textContent =
        'Heard: "' + heard + '" - setting fan to ' + LEVEL_NAMES[level] + ".";
      setLevel(level);
    }
  });

  recognition.addEventListener("error", (event) => {
    console.error("Speech error:", event.error);

    if (event.error === "not-allowed" || event.error === "service-not-allowed") {
      voiceFeedback.textContent =
        "Microphone access was blocked. Allow the microphone for this page and try again.";
    } else if (event.error === "no-speech") {
      voiceFeedback.textContent = "I didn't hear anything. Try again.";
    } else if (event.error === "audio-capture") {
      voiceFeedback.textContent = "No microphone was found.";
    } else if (event.error === "network") {
      voiceFeedback.textContent =
        "Speech recognition needs an internet connection.";
    } else if (event.error !== "aborted") {
      voiceFeedback.textContent = "Voice recognition error. Please try again.";
    }
  });

  recognition.addEventListener("end", () => {
    micBtn.classList.remove("listening");
    micBtn.textContent = "🎤 Voice command";
  });
}

/* Runs whenever someone signs in or out. */
onAuthStateChanged(auth, (user) => {
  if (user) {
    loginSection.hidden = true;
    controlSection.hidden = false;
    statusText.textContent = "Fan: connecting...";

    stopListening = onValue(
      powerRef,
      (snapshot) => {
        controlError.textContent = "";
        showLevel(snapshot.val());
      },
      (error) => {
        console.error("Read failed:", error.code);
        controlError.textContent = "Could not read the fan value.";
      }
    );
  } else {
    if (stopListening) {
      stopListening();
      stopListening = null;
    }
    controlSection.hidden = true;
    loginSection.hidden = false;
  }
});