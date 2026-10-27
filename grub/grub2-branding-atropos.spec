#
# spec file for package grub2-branding-atropos
#
# Live ISO boot menu only. The installed system uses systemd-boot and
# does not require this package.

Name:           grub2-branding-atropos
Version:        0.1.0
Release:        4
Summary:        AtropOS GRUB theme for the live ISO
# The menu pieces adapted from the openSUSE theme are CC-BY-SA-3.0.
# logo.png is the AtropOS crown, GPL-2.0-only.
License:        CC-BY-SA-3.0 AND GPL-2.0-only
Group:          System/Boot
URL:            https://github.com/ahjolinna/AtropOS-Linux
BuildArch:      noarch
Source0:        theme.txt
Source1:        highlight_c.png
Source2:        slider_c.png
Source3:        slider_n.png
Source4:        slider_s.png
Source5:        COPYING.CC-BY-SA-3.0
Source6:        logo.png
Provides:       grub2-branding

%description
Boot menu theme for the AtropOS live ISO. The openSUSE GRUB theme
has no background image, so this theme does not add one. The desktop
color is the openSUSE color. The green selection bar and scrollbar
are rose #c7080b. Those pieces are adapted from the openSUSE GRUB
theme. The corner mark is the AtropOS crown. The installed system
boots with systemd-boot and does not use this theme.

%prep

%build

%install
install -D -m 0644 %{SOURCE0} %{buildroot}%{_datadir}/grub2/themes/atropos/theme.txt
install -m 0644 %{SOURCE1} %{buildroot}%{_datadir}/grub2/themes/atropos/highlight_c.png
install -m 0644 %{SOURCE2} %{buildroot}%{_datadir}/grub2/themes/atropos/slider_c.png
install -m 0644 %{SOURCE3} %{buildroot}%{_datadir}/grub2/themes/atropos/slider_n.png
install -m 0644 %{SOURCE4} %{buildroot}%{_datadir}/grub2/themes/atropos/slider_s.png
install -m 0644 %{SOURCE5} %{buildroot}%{_datadir}/grub2/themes/atropos/COPYING.CC-BY-SA-3.0
install -m 0644 %{SOURCE6} %{buildroot}%{_datadir}/grub2/themes/atropos/logo.png
# Kiwi reads either the /usr theme or the /boot theme. One tree, two names.
mkdir -p %{buildroot}/boot/grub2/themes
ln -s %{_datadir}/grub2/themes/atropos %{buildroot}/boot/grub2/themes/atropos

%files
%defattr(-,root,root)
# grub2 is not in the OBS build root.
%dir %{_datadir}/grub2
%dir %{_datadir}/grub2/themes
%dir %{_datadir}/grub2/themes/atropos
%{_datadir}/grub2/themes/atropos/theme.txt
%{_datadir}/grub2/themes/atropos/highlight_c.png
%{_datadir}/grub2/themes/atropos/slider_c.png
%{_datadir}/grub2/themes/atropos/slider_n.png
%{_datadir}/grub2/themes/atropos/slider_s.png
%{_datadir}/grub2/themes/atropos/logo.png
%license %{_datadir}/grub2/themes/atropos/COPYING.CC-BY-SA-3.0
%dir /boot/grub2
%dir /boot/grub2/themes
/boot/grub2/themes/atropos

%changelog
