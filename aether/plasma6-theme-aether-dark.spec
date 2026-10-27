#
# spec file for package plasma6-theme-aether-dark
#
# Soft-fork of Ken Vermette's Aether Dark 0.0.3a
# (https://cgit.kde.org/scratch/kvermette/aether-plasma-theme-dark.git/).
# cgit.kde.org did not resolve. The source is the KDE Store snapshot
# aether-dark-0-0-3a.zip, product 1245521. The Plasma 6 port is local:
# metadata.json, the AetherDark color scheme id, adaptive transparency,
# selected+hover aliases, and a focus frame that reuses the hover art.

Name:           plasma6-theme-aether-dark
Version:        0.0.3a
Release:        8
Summary:        Aether Dark Plasma 6 theme
License:        LGPL-2.1-only
Group:          System/GUI/KDE
URL:            https://cgit.kde.org/scratch/kvermette/aether-plasma-theme-dark.git/
Source0:        %{name}-%{version}.tar.xz
BuildArch:      noarch
Requires:       kora-icon-theme
Requires:       papirus-icon-theme
Requires:       atropos-wallpapers
Requires:       plasma6-lookandfeel-atropos

%description
Aether Dark plasma style for Plasma 6, from Ken Vermette's 0.0.3a theme.
The global theme selects this style, the AetherDark color scheme
(openSUSE Dark with rose accent 199,8,11), Kora
icons, the AtropOS 02 wallpaper, and the AtropOS splash. Papirus
stays installed as the alternate icon theme. Window decorations, the
cursor, and the widget style stay Breeze. The task switcher is Cover
Switch. Widgets this theme does not ship fall back to Breeze.

%prep
%setup -q

%build

%install
theme=%{buildroot}%{_datadir}/plasma/desktoptheme/aetherdark
install -d "$theme" "$theme/dialogs" "$theme/icons" "$theme/widgets" \
    "$theme/widgets/dark" "$theme/widgets/light"
install -m 0644 metadata.json plasmarc colors LICENSE README.md "$theme/"
install -m 0644 dialogs/* "$theme/dialogs/"
install -m 0644 icons/* "$theme/icons/"
install -m 0644 widgets/*.svg "$theme/widgets/"
install -m 0644 widgets/dark/* "$theme/widgets/dark/"
install -m 0644 widgets/light/* "$theme/widgets/light/"
lnf=%{buildroot}%{_datadir}/plasma/look-and-feel/org.atropos.aether.desktop
install -D -m 0644 look-and-feel/org.atropos.aether.desktop/metadata.json \
    "$lnf/metadata.json"
install -D -m 0644 look-and-feel/org.atropos.aether.desktop/contents/defaults \
    "$lnf/contents/defaults"
install -D -m 0644 colors %{buildroot}%{_datadir}/color-schemes/AetherDark.colors

%files
%defattr(-,root,root)
%doc README.md
%license LICENSE
# plasma-workspace owns these directories on an installed system. The OBS
# build root does not, so this package owns them too.
%dir %{_datadir}/plasma
%dir %{_datadir}/plasma/desktoptheme
%{_datadir}/plasma/desktoptheme/aetherdark
%dir %{_datadir}/plasma/look-and-feel
%{_datadir}/plasma/look-and-feel/org.atropos.aether.desktop
%dir %{_datadir}/color-schemes
%{_datadir}/color-schemes/AetherDark.colors

%changelog
