# AtropOS

Plasma 6 splash screen. The gear and the slogan are Michail Vourlakos' sprites. The background is the AtropOS 02 wallpaper, cropped to fill. The package stays GPL-2.0-or-later. See `COPYING`.

The gear draws itself on, then “Experience Freedom!” writes in. The whole sequence finishes in 2 seconds, so it completes during a Plasma 6 login. The wallpaper file is 1376×768, so a 1920×1080 screen scales it up.

## Install

```
kpackagetool6 -t Plasma/LookAndFeel -i plasma6-lookandfeel-atropos-0.5
```

Then choose **AtropOS** in System Settings, Colors & Themes, Splash Screen. Choosing AtropOS as the global theme applies Aether Dark, Kora, the aetherdark style, the AtropOS 02 wallpaper, and this splash. The splash background is that same wallpaper.

Preview it without changing the splash you boot with:

```
ksplashqml --test --window org.atropos.desktop
```

Escape or a click closes the preview.

## Upgrade

```
kpackagetool6 -t Plasma/LookAndFeel -u plasma6-lookandfeel-atropos-0.5
```

## Remove

```
kpackagetool6 -t Plasma/LookAndFeel -r org.atropos.desktop
```
