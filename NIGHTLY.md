### What's new in this batch

Flash the **merged bin at 0x0** (erase first): each nightly is a fresh wipe-and-flash.

- **Smaller text:** the UI font is now unscii-16 at 8x16 (40 columns), so labels like
  SPREADING FACTOR are no longer cut off and more rows fit on a page.
- **Header on every page:** the clock and battery % sit on the right, and the title no longer
  runs under them.
- **CORE > ADVERT:** a third button between DMs and EDIT sends a MeshCore advert, FLOOD
  (repeaters pass it on) or LOCAL (zero hop). SETUP > MESHCORE > SEND ADVERT asks the same way.
- **SETUP > BATTERY:**
  - **RESET LEVEL COUNTER:** after a confirm, saves the voltage at full charge as 100 %. The
    level is voltage based; there is no fuel gauge.
  - **CHARGING SCREEN:** shows the logo and the charge level at 30 % brightness, with no screen
    sleep. Tap the trackball to exit; brightness and sleep come back.
- **SETUP > TIME:**
  - 12/24 h clock, set the date and time by hand, and a UTC offset. The header shows this time.
  - The T-Deck Plus has no battery-backed clock. The time is saved every 10 min and restored
    after power-off as an estimate (shown with `~`) until you set it again.
- **Favorites:** node popups in CORE and TASTIC Last Heard have **[ FAV ]**, and favorites
  show `*` in the lists.
- **CLEAR LAST HEARD** (SETUP > MESHTASTIC and > MESHCORE) asks **CLEAR ALL / KEEP FAVS /
  CANCEL**. On MeshCore, KEEP FAVS also keeps contacts you have DMs with.
- **Lock screen:** the prompt is two centred lines (LOCKED / hold trackball 3s) and no longer
  runs off the left edge.
