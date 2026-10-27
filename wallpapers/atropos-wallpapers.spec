#
# spec file for package atropos-wallpapers
#
# Still wallpapers and the silent video wallpaper for the installed
# desktop. The artwork is the packager's collection. The video plugin
# is GPL-2.0-or-later.

Name:           atropos-wallpapers
Version:        0.1.0
Release:        4
Summary:        AtropOS wallpapers and video wallpaper plugin
License:        LicenseRef-Proprietary AND GPL-2.0-or-later
Group:          System/GUI/KDE
URL:            https://github.com/ahjolinna/AtropOS-Linux
Source0:        %{name}-%{version}.tar.xz
BuildArch:      noarch
Requires:       qt6-multimedia
Requires:       qt6-multimedia-imports

%description
Still wallpapers for the installed AtropOS desktop, plus a silent video
and the Plasma wallpaper plugin that loops it. The global theme selects
AtropOS 02. AtropOS 01 and AtropOS 03 through 07 are the other stills.
The Video wallpaper type plays
/usr/share/atropos/wallpaper-animated.webm through Qt Multimedia.

%prep
%setup -q

%build

%install
install -d %{buildroot}%{_datadir}/wallpapers
install -d %{buildroot}%{_datadir}/plasma/wallpapers
install -d %{buildroot}%{_datadir}/atropos
install -m 0644 wallpaper-animated.webm \
    %{buildroot}%{_datadir}/atropos/wallpaper-animated.webm
cp -a org.atropos.video %{buildroot}%{_datadir}/plasma/wallpapers/
for id in atropos-01 atropos-02 atropos-03 atropos-04 atropos-05 atropos-06 atropos-07; do
    cp -a "$id" %{buildroot}%{_datadir}/wallpapers/
done
find %{buildroot} -type d -exec chmod 0755 {} +
find %{buildroot} -type f -exec chmod 0644 {} +

%files
%defattr(-,root,root)
%doc README.md
%license LICENSE
%license COPYING
# plasma-workspace owns these directories on an installed system. The
# OBS build root does not, so this package owns them too.
%dir %{_datadir}/wallpapers
%{_datadir}/wallpapers/atropos-01
%{_datadir}/wallpapers/atropos-02
%{_datadir}/wallpapers/atropos-03
%{_datadir}/wallpapers/atropos-04
%{_datadir}/wallpapers/atropos-05
%{_datadir}/wallpapers/atropos-06
%{_datadir}/wallpapers/atropos-07
%dir %{_datadir}/plasma
%dir %{_datadir}/plasma/wallpapers
%{_datadir}/plasma/wallpapers/org.atropos.video
%dir %{_datadir}/atropos
%{_datadir}/atropos/wallpaper-animated.webm

%changelog
