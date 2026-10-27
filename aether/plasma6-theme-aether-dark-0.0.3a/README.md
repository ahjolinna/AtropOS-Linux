# Aether Dark

Soft-fork of Ken Vermette's Aether Dark plasma theme, version 0.0.3a
(10 July 2018).

Upstream git is `https://cgit.kde.org/scratch/kvermette/aether-plasma-theme-dark.git/`.
That host did not resolve. The tree here is the KDE Store release
`aether-dark-0-0-3a.zip` (product 1245521), which is the published
snapshot of that repository. The zip also contained a `.git` directory
whose remote is `https://github.com/phobi4n/Indigo.git`. That history is
not Aether's and is not part of this fork.

License is the upstream LGPLv2.1 text in `LICENSE`. The artwork is
Ken Vermette's.

## Plasma 6

Plasma 6.8 reads `metadata.json`. The Plasma 5 `metadata.desktop` is not
installed.

`X-Plasma-API` is `5.0`, the same value Breeze ships. The old file said
`5.10`. The contrast block (`enabled=false`) is not installed. libPlasma
6.8 no longer reads `ContrastEffect`.

The color values are unchanged. The scheme id was `Breeze_Transparent`
in the published file. It is now `AetherDark`, so it does not claim to
be a Breeze scheme. The same file is installed as
`/usr/share/color-schemes/AetherDark.colors`.

`plasmarc` enables adaptive transparency, as the Plasma 6 Breeze themes do.

`widgets/viewitem.svg` adds `selected+hover` elements that reference the
existing `selected_hover` art. Plasma 6's highlight item asks for the
plus-sign id.

`widgets/button.svg` adds `focus` elements that reference the existing
hover art. Plasma 6's button focus frame asks for `focus`, which this
theme did not draw.

Widgets this theme never shipped still fall back to Breeze.

The global theme `org.atropos.aether.desktop` selects this plasma style,
the AetherDark color scheme, the Kora icon theme, and the AtropOS 02
wallpaper from `atropos-wallpapers`. The login splash is AtropOS
(`org.atropos.desktop`). The task switcher is Cover Switch.
Papirus stays installed as the alternate. Window decorations, the
cursor, and the widget style stay Breeze. The other stills and the
silent video ship in that wallpaper package. The video plays when the
Video wallpaper type is selected.
