#
# spec file for package AtropOS-release
#
# Rewritten from the 20260608 AtropOS-release package. The os-release
# shape follows current Kalpa/Tumbleweed: a date VERSION_ID, CPE 2.3,
# and /etc/os-release as a symlink into /usr.

Name:           AtropOS-release
Version:        20261020
Release:        5
Summary:        AtropOS
License:        GPL-2.0-or-later
Group:          System/Fhs
URL:            https://github.com/AtropOSLinux/
Source1:        LICENSE
Source2:        gpg-pubkey-home-ahjolinna.asc
Source3:        atropos.repo
Source4:        com.github.atropos.metainfo.xml
Conflicts:      distribution-release
Provides:       distribution-release
Provides:       suse-release = %{version}-%{release}
Provides:       suse-release-oss = %{version}-%{release}
# zypp keeps this product current with dup, not up (boo#1061384)
Provides:       product-update() = dup
Suggests:       distribution-logos-openSUSE-MicroOS

%description
AtropOS is a rolling immutable Plasma desktop. The base system is
read-only and updates atomically. Applications come from Flatpak.

%prep

%build

%install
mkdir -p %{buildroot}%{_prefix}/lib/issue.d
mkdir -p %{buildroot}%_defaultlicensedir/%{name}
mkdir -p %{buildroot}%_defaultlicensedir/product/base

printf 'Welcome to AtropOS %s - Kernel \\r (\\l).\n\n' '%{version}' \
    > %{buildroot}%{_prefix}/lib/issue.d/10-AtropOS.issue
printf '\n' > %{buildroot}%{_prefix}/lib/issue.d/90-AtropOS.issue

install -m 0644 %{SOURCE1} %{buildroot}%_defaultlicensedir/%{name}/LICENSE
install -m 0644 %{SOURCE1} %{buildroot}%_defaultlicensedir/product/base/license.txt

# VERSION is informative only on a rolling system, so it is omitted.
cat > %{buildroot}%{_prefix}/lib/os-release <<EOF
NAME="AtropOS"
ID="atropos"
ID_LIKE="suse opensuse opensuse-slowroll opensuse-tumbleweed opensuse-microos opensuse-kalpa"
VERSION_ID="%{version}"
PRETTY_NAME="AtropOS"
ANSI_COLOR="0;32"
# CPE 2.3
CPE_NAME="cpe:2.3:o:atropos:atropos:%{version}:*:*:*:*:*:*:*"
# CPE 2.2
#CPE_NAME="cpe:/o:atropos:atropos:%{version}"
BUG_REPORT_URL="https://github.com/AtropOSLinux/"
SUPPORT_URL="https://github.com/AtropOSLinux/"
HOME_URL="https://github.com/AtropOSLinux/"
DOCUMENTATION_URL="https://github.com/AtropOSLinux/"
LOGO="distributor-logo"
EOF
mkdir -p %{buildroot}%{_sysconfdir}
ln -s ..%{_prefix}/lib/os-release %{buildroot}%{_sysconfdir}/os-release

install -D -m 0644 %{SOURCE2} %{buildroot}%{_prefix}/lib/rpm/gnupg/keys/gpg-pubkey-home-ahjolinna.asc
install -D -m 0644 %{SOURCE3} %{buildroot}%{_sysconfdir}/zypp/repos.d/atropos.repo
# Discover looks up the distro by reversing HOME_URL and appending ID.
# https://github.com/AtropOSLinux/ plus ID=atropos is com.github.atropos.
# The release version matches VERSION_ID so Discover does not offer an upgrade.
install -D -m 0644 %{SOURCE4} %{buildroot}%{_datadir}/metainfo/com.github.atropos.metainfo.xml

%post
# Agama imports this file itself. The installed system uses the rpm keyring.
rpm --import %{_prefix}/lib/rpm/gnupg/keys/gpg-pubkey-home-ahjolinna.asc || :

%files
%defattr(644,root,root,755)
%dir %_defaultlicensedir/%{name}
%license %_defaultlicensedir/%{name}/LICENSE
%dir %_defaultlicensedir/product
%dir %_defaultlicensedir/product/base
%_defaultlicensedir/product/base/license.txt
%{_sysconfdir}/os-release
%{_prefix}/lib/os-release
%dir %{_prefix}/lib/issue.d
%{_prefix}/lib/issue.d/*-AtropOS.issue
# rpm and libzypp own these directories when they are installed. The OBS
# build root for this package does not, so own them here too.
%dir %{_prefix}/lib/rpm
%dir %{_prefix}/lib/rpm/gnupg
%dir %{_prefix}/lib/rpm/gnupg/keys
%{_prefix}/lib/rpm/gnupg/keys/gpg-pubkey-home-ahjolinna.asc
%dir %{_sysconfdir}/zypp
%dir %{_sysconfdir}/zypp/repos.d
%config(noreplace) %{_sysconfdir}/zypp/repos.d/atropos.repo
# appstream may own this directory. The OBS build root for this package
# does not, so own it here too.
%dir %{_datadir}/metainfo
%{_datadir}/metainfo/com.github.atropos.metainfo.xml

%changelog
