#
# spec file for package zsh-history-substring-search
#
# Fish-like up-arrow history search. 1.1.0 is the latest release.
# The license is BSD-3-Clause, in the script header. The source RPM
# this is based on called it MIT.

Name:           zsh-history-substring-search
Version:        1.1.0
Release:        1
Summary:        Fish-like history search for zsh
License:        BSD-3-Clause
Group:          System/Shells
URL:            https://github.com/zsh-users/zsh-history-substring-search
Source0:        %{name}-%{version}.tar.gz
BuildArch:      noarch
Requires:       zsh >= 4.3

%description
Type part of an earlier command and press Up or Down to cycle through
matching history. The search is a substring, as in fish.

%prep
%autosetup

%build

%install
install -d %{buildroot}%{_datadir}/%{name}
install -pm 644 zsh-history-substring-search.zsh \
    zsh-history-substring-search.plugin.zsh \
    %{buildroot}%{_datadir}/%{name}/

%files
%doc README.md
%{_datadir}/%{name}

%changelog
