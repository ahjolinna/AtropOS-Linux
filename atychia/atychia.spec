#
# spec file for package atychia
#
# Soft-fork of https://invent.kde.org/tduck/atychia (0.19).
# Ctrl+Alt+Shift+Esc asks systemd-logind for the Secure Attention Key
# and opens a recovery screen on its own TTY. Logout returns to the
# previous TTY, so it does not require SDDM.

Name:           atychia
Version:        0.19
Release:        8
Summary:        Recover a broken Plasma session without a TTY
License:        GPL-2.0-or-later AND BSD-3-Clause AND CC0-1.0 AND FSFAP
Group:          System/GUI/KDE
URL:            https://invent.kde.org/tduck/atychia
Source0:        %{name}-%{version}.tar.xz
Source1:        81-atychia.preset
Source2:        atychia-as-root
Source3:        atropos-atychia.sudoers
Source4:        z-atropos-atychia.conf
BuildRequires:  cmake
BuildRequires:  ninja
BuildRequires:  gcc-c++
BuildRequires:  kf6-extra-cmake-modules
# The cmake_kf6 and kf6_build macros live in kf6-filesystem. Slowroll has no
# kf6-rpm-macros package.
BuildRequires:  kf6-filesystem
BuildRequires:  gettext-tools
BuildRequires:  systemd-rpm-macros
BuildRequires:  cmake(Qt6Core)
BuildRequires:  cmake(Qt6Gui)
BuildRequires:  cmake(Qt6Qml)
BuildRequires:  cmake(Qt6Quick)
BuildRequires:  cmake(Qt6QuickControls2)
BuildRequires:  cmake(Qt6Svg)
BuildRequires:  cmake(Qt6DBus)
BuildRequires:  cmake(Qt6Widgets)
BuildRequires:  cmake(KF6Kirigami)
BuildRequires:  cmake(KF6CoreAddons)
BuildRequires:  cmake(KF6I18n)
BuildRequires:  cmake(KF6KirigamiAddons)
BuildRequires:  cmake(KF6ColorScheme)
BuildRequires:  cmake(KF6Service)
BuildRequires:  cmake(KF6KIO)
Requires:       kwin6
Requires:       polkit
Requires:       sudo
Requires:       kbd
Requires:       bash
Requires:       systemd
Requires:       kf6-qqc2-desktop-style
# Slowroll has no qt6qml() providers. The QML modules ship in these packages.
Requires:       kf6-kirigami
Requires:       kf6-kirigami-imports
Requires:       kirigami-addons6

%description
A recovery screen for a stuck Plasma session, bound to systemd-logind's
Secure Attention Key (Ctrl+Alt+Shift+Esc). It starts KWin on a free TTY
and can return to the previous session. It does not replace the display
manager. The chord starts that screen through sudo, because pkexec is
not installed, and the login manager does not also open the greeter.

%prep
%setup -q
# Ninja rebuilds build.ninja forever when a source timestamp is ahead
# of the worker clock, then exits with "perhaps system time is not set".
# A changelog time later the same day does that. Stamp those files now.
find . -newermt "@$(date +%%s)" -exec touch {} +

%build
%cmake_kf6
%kf6_build

%install
%kf6_install
# The QML module is compiled into /usr/bin/atychia. A second copy under
# the QML import path is not required, and Qt's install set varies.
rm -rf %{buildroot}%{_libdir}/qt6/qml/org/filotimoproject
rmdir %{buildroot}%{_libdir}/qt6/qml/org 2>/dev/null || :
install -D -m 0644 %{SOURCE1} %{buildroot}%{_prefix}/lib/systemd/user-preset/81-atychia.preset
# systemctl --user on the absolute path of this unit looks up
# usr-lib-systemd-user-atychiad.service.mount, which does not exist,
# so atychiad.service never starts. Enable it by unit name. The link
# is static, so the first boot does not depend on the preset scriptlet.
mkdir -p %{buildroot}%{_userunitdir}/default.target.wants
ln -s ../atychiad.service %{buildroot}%{_userunitdir}/default.target.wants/atychiad.service
install -D -m 0755 %{SOURCE2} %{buildroot}%{_libexecdir}/atychia-as-root
install -D -m 0440 %{SOURCE3} %{buildroot}%{_sysconfdir}/sudoers.d/atropos-atychia
install -D -m 0644 %{SOURCE4} %{buildroot}%{_datadir}/dbus-1/system.d/z-atropos-atychia.conf

%pre
%systemd_user_pre atychiad.service

%post
%systemd_user_post atychiad.service

%preun
%systemd_user_preun atychiad.service

%postun
%systemd_user_postun atychiad.service

%files
%license LICENSES/GPL-2.0-or-later.txt
%license LICENSES/BSD-3-Clause.txt
%license LICENSES/CC0-1.0.txt
%license LICENSES/FSFAP.txt
%{_bindir}/atychia
%{_bindir}/atychiad
%{_bindir}/atychia-launch
%{_userunitdir}/atychiad.service
# systemd owns /usr/lib/systemd/user. It does not ship this wants
# directory. Own it so the OBS build root does not fail the package.
%dir %{_userunitdir}/default.target.wants
%{_userunitdir}/default.target.wants/atychiad.service
%{_datadir}/polkit-1/actions/org.filotimoproject.atychia-launch.policy
%dir %{_prefix}/lib/systemd/user-preset
%{_prefix}/lib/systemd/user-preset/81-atychia.preset
%{_libexecdir}/atychia-as-root
%dir %{_sysconfdir}/sudoers.d
%attr(0440,root,root) %config(noreplace) %{_sysconfdir}/sudoers.d/atropos-atychia
%dir %{_datadir}/dbus-1
%dir %{_datadir}/dbus-1/system.d
%{_datadir}/dbus-1/system.d/z-atropos-atychia.conf

%changelog
