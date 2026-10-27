#
# spec file for package plymouth-theme-atropos
#
# Boot splash. The gear and slogan artwork is Michail Vourlakos' Fuuten
# theme, GPL-2.0-or-later. /etc/plymouth/plymouthd.conf selects this theme.
# plymouth-branding-openSUSE still provides plymouth-branding and bgrt, which
# is used again when this package is removed.

Name:           plymouth-theme-atropos
Version:        0.1.0
Release:        3
Summary:        AtropOS Plymouth boot splash
License:        GPL-2.0-or-later
Group:          System/Base
URL:            https://github.com/AtropOSLinux/
Source0:        %{name}-%{version}.tar.xz
Source1:        plymouthd.conf
BuildArch:      noarch
Requires:       plymouth
Requires:       plymouth-plugin-script
Requires:       plymouth-plugin-label

%description
AtropOS boot splash. A black screen plays the gear, then
"Experience Freedom!". The passphrase prompt sits in the middle of the screen.
There is no vendor logo.

The package selects this theme. The openSUSE branding package remains
installed, and its bgrt theme is the fallback after this package is removed.

%prep
%setup -q

%build

%install
install -d %{buildroot}%{_datadir}/plymouth/themes/atropos
install -m 0644 atropos.plymouth atropos.script *.png \
    %{buildroot}%{_datadir}/plymouth/themes/atropos/
install -d %{buildroot}%{_defaultlicensedir}/%{name}
install -m 0644 COPYING %{buildroot}%{_defaultlicensedir}/%{name}/COPYING
install -D -m 0644 %{SOURCE1} %{buildroot}%{_sysconfdir}/plymouth/plymouthd.conf

%files
%defattr(-,root,root)
# The OBS build root does not contain plymouth, so these directories are
# unowned there. Share them with plymouth on an installed system.
%dir %{_defaultlicensedir}/%{name}
%license %{_defaultlicensedir}/%{name}/COPYING
%doc README.md
%dir %{_datadir}/plymouth
%dir %{_datadir}/plymouth/themes
%dir %{_datadir}/plymouth/themes/atropos
%{_datadir}/plymouth/themes/atropos/atropos.plymouth
%{_datadir}/plymouth/themes/atropos/atropos.script
%{_datadir}/plymouth/themes/atropos/*.png
%dir %{_sysconfdir}/plymouth
%config(noreplace) %{_sysconfdir}/plymouth/plymouthd.conf

%changelog
