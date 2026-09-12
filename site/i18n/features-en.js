export default {
  "home": {
    "name": "Home",
    "summary": "A small device. A complete field toolkit.",
    "detail": "Open a feature directly on the device. Pager uses a horizontal focus carousel; T-Deck uses a compact grid with touch and keyboard input.",
    "needs": "Pager: encoder and keyboard. T-Deck: touch, trackball and keyboard."
  },
  "map": {
    "name": "Offline maps",
    "summary": "Keep your bearings beyond cell coverage.",
    "detail": "Browse north-up OSM, terrain and satellite layers from SD storage. Switch layers, add contours, zoom and inspect a node while retaining the surrounding map context.",
    "needs": "Prepare the map region on SD before departure. This preview includes nine cached OSM tiles at zoom 12; other regions, layers and zoom levels need their own tile data."
  },
  "gps": {
    "name": "GNSS / Sky plot",
    "summary": "Know how trustworthy your fix is.",
    "detail": "Inspect satellite geometry, constellation, signal strength, used satellites and HDOP. A visible satellite is not necessarily used in the current position fix.",
    "needs": "GNSS hardware and an antenna with a clear view of the sky. All coordinates and signal values in this guide are samples."
  },
  "tracker": {
    "name": "Track recorder",
    "summary": "Keep a local record of the way you came.",
    "detail": "Start and stop a track recording, browse saved tracks and export GPX through USB storage. Local tracks and shared Team positions are separate data relationships.",
    "needs": "Usable GNSS positions and writable storage. The preview records sample positions into its browser-local virtual filesystem, which resets on reload."
  },
  "route": {
    "name": "Routes & elevation",
    "summary": "Understand the climb before following the trail.",
    "detail": "Inspect imported KML routes, review the elevation profile, then step through georeferenced waypoint imagery. Distance, load state and saved-image counts remain visible.",
    "needs": "Import the route and prepare its map region. Remote waypoint imagery requires connectivity when not already cached."
  },
  "chat": {
    "name": "Messages",
    "summary": "Send a message on your chosen network.",
    "detail": "Browse conversations and compose direct, channel or team messages. Delivery states matter on narrow, lossy links; a failed send can be retried from its message bubble.",
    "needs": "Choose one active protocol and a compatible peer/network profile. Demo messages never leave this page."
  },
  "contacts": {
    "name": "Contacts & identity",
    "summary": "Keep your communication directory on the device.",
    "detail": "Inspect discovered and saved contacts, recent activity and protocol identity. Reticulum peers expose an LXMF address and identity hash. Available messaging, ping, call and team actions depend on the selected protocol.",
    "needs": "Saved contacts or discovery results. Public discovery, contact sharing and Team sharing have separate scopes."
  },
  "team": {
    "name": "TAK / Team",
    "summary": "See the people and places that matter to your group.",
    "detail": "Create or join a nearby team over ESP-NOW, then use LoRa for team messages, member positions, status and assembly points. The current TAK scope is Trail Mate’s own on-device awareness.",
    "needs": "Pair together at close range. ATAK, WinTAK and CoT interoperability are outside the current TAK claim."
  },
  "walkie": {
    "name": "Walkie talkie",
    "summary": "Press to talk. Release to listen.",
    "detail": "The half-duplex voice path uses FSK and Codec2 with jitter buffering and a fixed playback cadence. Monitoring and push-to-talk are separate controls.",
    "needs": "A build and board with a supported radio/audio path. This browser demo does not access your microphone or transmit audio."
  },
  "probe": {
    "name": "Protocol Probe",
    "summary": "Find evidence of a compatible LoRa profile.",
    "detail": "Probe finite protocol-derived profiles and distinguish OBSERVED from CONFIRMED evidence. MeshCore can actively confirm; Meshtastic needs a suitable keyed acknowledgement. Reticulum stays passive in this temporary tuning flow.",
    "needs": "Real protocol traffic is required on hardware. A quiet frequency is not evidence of a usable channel."
  },
  "sstv": {
    "name": "SSTV receiver",
    "summary": "Watch an image emerge from received audio.",
    "detail": "Receive supported slow-scan television audio and decode an image on-device, with visible progress and a preview.",
    "needs": "Supported audio input and build. The sample illustrates progress; it does not decode audio."
  },
  "network": {
    "name": "Nomad Network",
    "summary": "Small information services over Reticulum.",
    "detail": "Open Micron pages, navigate links and forms, and retain cached/offline-ready state. Compatibility diagnostics identify unsupported or unknown Micron constructs.",
    "needs": "Reticulum mode and a reachable service for uncached pages. This is a sample Micron experience, not a general web browser."
  },
  "mqtt": {
    "name": "Mesh MQTT",
    "summary": "Extend a mesh through a configured broker.",
    "detail": "Meshtastic and MeshCore have independent enable, uplink, downlink, host, port and topic settings. An Internet gateway is optional; the mesh protocol remains explicit.",
    "needs": "Wi-Fi or gateway connectivity and an appropriate broker. Credentials are not requested in this demo."
  },
  "usb": {
    "name": "USB storage",
    "summary": "Move maps and tracks through removable storage.",
    "detail": "Expose supported device storage to a computer for file management and GPX export. Storage ownership changes when the host mounts the volume.",
    "needs": "Supported USB storage target and a data cable. The demo does not access files on your computer."
  },
  "extensions": {
    "name": "Extensions",
    "summary": "Carry the languages you need.",
    "detail": "Install translation, font and keyboard bundles instead of embedding every language in firmware. English remains the built-in fallback; the published catalog keeps language review status visible.",
    "needs": "Compatible memory profile, storage and a reachable package repository on hardware. The preview shows a sample catalog; it does not install packages on a device."
  },
  "settings": {
    "name": "Settings",
    "summary": "Make the device work for your trip.",
    "detail": "Adjust display and sleep behavior, GNSS, networking and the active product protocol. Meshtastic, MeshCore and Reticulum are selectable paths; only one is active at a time.",
    "needs": "Hardware and protocol-specific parameters vary by build. Real radio settings must match your local requirements."
  },
  "calculator": {
    "name": "Field calculator",
    "summary": "Turn angles and distances into useful field estimates.",
    "detail": "Use sin/cos/tan and inverse functions to estimate height, resolve slope distance into horizontal distance and rise, or calculate a gradient. The firmware also provides powers, roots, logarithms and DEG/RAD modes.",
    "needs": "Enter angles and distances from your own measurements. For tree height, add instrument height only when the base and your ground are level. The device performs the calculation; it does not measure the angle or distance automatically."
  },
  "help": {
    "name": "Help & shortcuts",
    "summary": "Keep the controls close at hand.",
    "detail": "The device’s context help explains the current page and hardware controls. Pager navigation and T-Deck touch/trackball affordances differ.",
    "needs": "Web controls are documented separately from hardware shortcuts. Check the device Help screen for build-specific keys."
  },
  "power": {
    "name": "Status & power",
    "summary": "Check the essentials before heading out.",
    "detail": "Review battery and runtime status, use low-power behavior and shut down gracefully.",
    "needs": "Battery, charging and power behavior depend on the board. The browser only changes its simulated screen state."
  }
};
