#
# spec file for package zsh-autosuggestions
#
# Fish-like suggestions as you type. 0.7.1 is the latest release.
# The 0.7.0 package this is based on was built as i586 and installed
# both wrapper files by hand.

Name:           zsh-autosuggestions
Version:        0.7.1
Release:        1
Summary:        Fish-like autosuggestions for zsh
License:        MIT
Group:          System/Shells
URL:            https://github.com/zsh-users/zsh-autosuggestions
Source0:        %{name}-%{version}.tar.gz
BuildArch:      noarch
Requires:       zsh >= 4.3.11

%description
Suggests commands as you type, from history and completions. Accept
the suggestion with Right or End.

%prep
%autosetup

%build
# The release archive already contains the generated script.

%install
install -d %{buildroot}%{_datadir}/%{name}
install -pm 644 zsh-autosuggestions.zsh zsh-autosuggestions.plugin.zsh \
    %{buildroot}%{_datadir}/%{name}/

%files
%license LICENSE
%doc README.md CHANGELOG.md
%{_datadir}/%{name}

%changelog
