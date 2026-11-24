// Scooby Deck v3: our own T-Deck Plus firmware. Upstream MeshCore (MIT) as a library, the
// dual-mesh time slicing after SquachWatch-CYD (GPL-3.0), Meshtastic (GPL-3.0) protocol, and
// hardware drivers borrowed from Faro (Apache-2.0). See NOTICE.
#pragma once
#define SCOOBY_DECK_NAME    "Scooby Deck"
#define SCOOBY_DECK_VERSION "3.0.0-dev"

// The ASCII banner of the boot screen and the lock screen.
#define SCOOBY_BANNER \
  " ___  ___ ___   ___  _____   __\n" \
  "/ __|/ __/ _ \\ / _ \\| _ ) \\ / /\n" \
  "\\__ \\ (_| (_) | (_) | _ \\\\ V / \n" \
  "|___/\\___\\___/ \\___/|___/ |_|  \n" \
  "     ___  ___ ___ _  __\n" \
  "    |   \\| __/ __| |/ /\n" \
  "    | |) | _| (__| ' < \n" \
  "    |___/|___\\___|_|\\_\\"
#define SCOOBY_CREDITS \
  "> MESHCORE (MIT) + MESHTASTIC PROTOCOL\n" \
  "> DUAL-MESH AFTER SQUACHWATCH (GPL-3.0)\n" \
  "> T-DECK DRIVERS FROM FARO (APACHE-2.0)\n" \
  "> SCOOBY DECK IS GPL-3.0"
