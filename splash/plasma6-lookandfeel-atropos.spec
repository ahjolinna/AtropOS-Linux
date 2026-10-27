#
# spec file for package plasma6-lookandfeel-atropos
#
# Plasma 6 splash. The gear and slogan are Michail Vourlakos' sprites.
# The background is the AtropOS 02 wallpaper.
# The look-and-feel id is org.atropos.desktop.

Name:           plasma6-lookandfeel-atropos
Version:        0.5
Release:        6
Summary:        AtropOS Plasma 6 splash screen
License:        GPL-2.0-or-later
Group:          System/GUI/KDE
URL:            https://github.com/AtropOSLinux/
Source0:        %{name}-%{version}.tar.xz
BuildArch:      noarch
Requires:       atropos-wallpapers

%description
AtropOS login splash and global theme for Plasma 6. Choosing AtropOS
applies Aether Dark, the aetherdark style, Kora icons, the AtropOS 02
wallpaper, and this splash. The splash background is that same
AtropOS 02 wallpaper, cropped to fill. The gear draws itself on,
then the slogan writes in.
The sequence finishes in about two seconds. Window decorations, the
cursor, and the widget style stay Breeze.

%prep
%setup -q

%build

%install
dest=%{buildroot}%{_datadir}/plasma/look-and-feel/org.atropos.desktop
install -d "$dest"
install -m 0644 metadata.json "$dest/"
cp -a contents "$dest/"
find %{buildroot} -type d -exec chmod 0755 {} +
find %{buildroot} -type f -exec chmod 0644 {} +

%files
%defattr(-,root,root)
%doc README.md
%license COPYING
# plasma-workspace owns these directories on an installed system. The
# OBS build root does not, so this package owns them too.
%dir %{_datadir}/plasma
%dir %{_datadir}/plasma/look-and-feel
%{_datadir}/plasma/look-and-feel/org.atropos.desktop

%changelog
