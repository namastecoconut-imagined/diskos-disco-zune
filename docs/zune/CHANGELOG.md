# Zune theme changelog

Changes to the Zune theme for diskOS Disco! on the FiiO Snowsky Disc. The newest update is at the top.

## Update 3 (October 2026)

Functional fixes brought over from zmd22's diskOS Disco! 3.0 work. Thanks, zmd22. The look of the theme is unchanged.

- **Bluetooth codecs.** Headphones connect on the codec you chose from the start, and the Disc remembers which codecs each pair really supports. After a real disconnect, the next connection starts clean. A "Connected: headphones / codec" note appears once the codec has settled, including on automatic reconnects.
- **Bluetooth screen.** It now updates when headphones connect or disconnect while it's open, so it no longer shows a stale "Connected – LDAC".
- **Safer USB-C output.** After switching, the Disc waits until a DAC that can actually play has appeared and gone quiet, then connects to it and resumes. If none appears within 8 s, it returns to the headphone jack. Recovery never re-routes while audio is still playing. Bluetooth can't take over while on USB-C, and other working modes ask you to leave USB-C first.
- **Volume overlay.** It takes the touch while it's open, so turning the volume can't seek the song or press anything underneath. It stays up while your finger is on it, and a tap closes it.
- **Dialogs.** Long titles shrink to fit and long text scrolls.
- **Long list names** in dial lists step down a size instead of running off the glass.
- **Small fixes.**
  - Leaving Immersive mid-animation no longer leaves the disc settling.
  - The back-swipe arrow from the left edge no longer shows "too late" by mistake.
  - Scrolling labels in Listening Stats and Tune are no longer clipped.
  - The lock screen's progress bar stops updating while the screen is dark.

## Update 2 (October 2026)

This is everything since the first release of the theme.

### New

- **Typographic Now Playing.** The cover is bigger. The title and artist sit in large type, with the time and the file format underneath.
- **Immersive view.** Swipe up on Now Playing, or tap the IMMERSIVE link under the controls, for the full-screen cover. The controls appear when it opens; tap to hide or show them. It can also show synced lyrics.
- **Quickplay.** A new item on Home shows the albums you played last ("history") and the albums added to the card last ("new"). Tap a cover to open the album, or hold it to play.
- **Home background.** The playing album, blurred and dimmed, drifts slowly behind the Home list. You can turn it off under Settings > Display > Appearance.
- **Metro lock screen.** A big clock with the date, the weather, and what's playing, with its progress.
- **Desk clock.** While the Disc charges, the lock screen stays on, dimmed, as a clock with the weather and the song. It's on by default; you can turn it off under Settings > Display > Standby.
- **Light mode.** A light variant of the theme, which can switch with the time of day.
- **New boot splash.** "disc" appears as soon as the screen is ready, and covers from your card fill a grid behind it.
- **Power-off screen.** When you hold the power key, the boot screen plays in reverse: the accent ring drains while "shutting down" shows.
- **Gesture guide.** On the first start, five short cards explain back, quick settings, Now Playing gestures, rim scrolling and letter jump. To see them again, go to Settings > Help > Gesture Guide.
- **Resume Last Song.** By default the Disc now starts with an empty queue. To load the last song at start-up as before, turn this on under Settings > Playback.
- **Sleep timer ring.** A thin ring on the rim shrinks while a sleep timer runs.
- **Headphone battery.** On the Bluetooth screen, a ring round the rim shows the connected headphones' battery, when they report it.
- **Weather now.** The weather screen highlights the current hour.
- **Audio explainers.** Each audio option (gain, DAC filter, ReplayGain and so on) has a short line explaining what it does.

### Touch and gestures

- **Rim scrolling everywhere.** Drag your finger round the edge to scroll any list, including pop-ups like the output picker and song menu. A list keeps gliding for a moment after you let go.
- **Letter jump on the rim.** In long lists, drag the round letter on the right and A–Z appears around the rim. Let go on a letter to jump to it. A plain tap still opens the letter grid.
- **Rim keyboard.** You type by sliding round the rim, with a big preview of the letter you're on. There's now a space key.
- **Bigger touch targets.** Album tracks, links, equalizer controls, accent swatches, weather, queue, Wi-Fi and Bluetooth rows, and the keyboard's cancel are easier to hit.
- **Equalizer.** Big − and + buttons, with arrows to move between bands. The tiny band handles no longer need precise taps.
- **Back swipe.** A swipe in from the left edge now has up to a second to finish.
- **Motion.** Screens turn in and out in the Windows Phone 7 style. Quick settings slide down from the top. Pop-ups drop in. A touched list row gives a little.

### Fixes

- The output picker no longer gets stuck. Back always closes a pop-up before leaving the screen.
- Shuffle All now starts on a random song instead of always the first.
- The screen no longer dims a little on its own after start-up.
- Idle power-off works again.
- Touch is read more often and the screen redraws faster, so scrolling feels quicker.
- Start-up is faster.

### Removed

- **Last.fm, Boomshine, SongSphere and Album Roulette.** These are removed from the code entirely, which makes the program about 7% smaller. Listening Stats and installed apps still work.
- **The right-edge swipe on Home that opened Apps.** Apps is still in the Home list.

## Update 1

This was the first release: the Metro-style Zune theme, the looping dial for Home and every list, the USB-C audio mode, the output picker and the themed lock screen. See [ZUNE.md](../ZUNE.md).
