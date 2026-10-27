# AtropOS Plymouth theme

Boot splash for AtropOS. The screen is black. The gear draws, then “Experience Freedom!” writes in. The Plasma look-and-feel in `/home/ahjolinna/Projects/fuuten` is unchanged. This theme does not use the poster or a vendor logo.

The slogan cell stays 300 by 163 design pixels from the gear cell, which is the offset in `Splash.qml`. The pair is centered. It is scaled to fit 86% of the screen width and 42% of the screen height, whichever is smaller. The passphrase, question, and Plymouth message sit just above the slogan, centered on it. Boot status lines are not drawn.

Playback is 55 gear frames in 1.1 s, then 45 slogan frames in 0.9 s, at Plymouth’s 50 Hz refresh. The last frame stays up for the rest of boot.

Artwork by Michail Vourlakos, GPL-2.0-or-later. See `COPYING`.

## OBS

Upload these files from `/home/ahjolinna/Projects/atropos/plymouth`:

- `plymouth-theme-atropos.spec`
- `plymouth-theme-atropos.changes`
- `plymouth-theme-atropos-0.1.0.tar.xz`
- `plymouthd.conf` (Source1; it stays outside the tarball)

The package installs the theme and writes `/etc/plymouth/plymouthd.conf` with `Theme=atropos`. Plymouth reads that file before the branding default. `plymouth-branding-openSUSE` stays installed, so `bgrt` is the fallback after this package is removed. The package does not rebuild the initrd. Dracut and the image build pick the theme up from that file.

## Frames

```
bash scripts/extract-frames.sh
```

Reads the sprite sheets from the Plasma theme and writes `theme/gear-0.png` through `theme/gear-54.png` and `theme/text-0.png` through `theme/text-44.png`. Rebuild the tarball after changing frames or the script.

## Stills

```
bash scripts/preview.sh 1920 1080 27 -1 previews/1080-gear.png
bash scripts/preview.sh 1920 1080 54 44 previews/1080-password.png "Enter passphrase for disk"
```
