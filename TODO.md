# KDE Linux ports

Done. These drop-ins from
[KDE Linux](https://invent.kde.org/kde-linux/kde-linux) `master` are in
`atropos-performance` 0.4.0, except the NVIDIA power tweaks, which are in
`atropos-nvidia` 0.1.0. Links point at the source files.

- [x] **ntsync** — faster Wine and Proton.
  [ntsync.conf](https://invent.kde.org/kde-linux/kde-linux/-/blob/master/mkosi.extra/usr/lib/modules-load.d/ntsync.conf)

- [x] **BFQ on hard disks** — lower latency on spinning drives. SSDs stay as they are.
  [60-ioschedulers.rules](https://invent.kde.org/kde-linux/kde-linux/-/blob/master/mkosi.extra/usr/lib/udev/rules.d/60-ioschedulers.rules)

- [x] **USB writeback cap** — a slow USB disk does not stall the rest of the system.
  [60-usb-dirty-pages-udev.rules](https://invent.kde.org/kde-linux/kde-linux/-/blob/master/mkosi.extra/usr/lib/udev/rules.d/60-usb-dirty-pages-udev.rules),
  [usb-dirty-pages-udev](https://invent.kde.org/kde-linux/kde-linux/-/blob/master/mkosi.extra/usr/lib/usb-dirty-pages-udev)

- [x] **HDA power save** — no speaker crackle on AC power. Power save returns on battery.
  [20-audio-pm.conf](https://invent.kde.org/kde-linux/kde-linux/-/blob/master/mkosi.extra/usr/lib/modprobe.d/20-audio-pm.conf),
  [20-audio-pm.rules](https://invent.kde.org/kde-linux/kde-linux/-/blob/master/mkosi.extra/usr/lib/udev/rules.d/20-audio-pm.rules)

- [x] **Watchdog module blacklist** — matches `nowatchdog`. Slightly faster boot and less idle power.
  [98-kde-linux-watchdog.conf](https://invent.kde.org/kde-linux/kde-linux/-/blob/master/mkosi.extra/usr/lib/modprobe.d/98-kde-linux-watchdog.conf)

- [x] **PAM memlock** — SSH and tty sessions get the same unlocked-memory limit the desktop already has, which audio workstations need.
  [00-kde-linux-default.conf](https://invent.kde.org/kde-linux/kde-linux/-/blob/master/mkosi.extra/etc/security/limits.d/00-kde-linux-default.conf)

- [x] **KillUserProcesses** — logout ends leftover user processes.
  [00-kde-linux-killuserprocesses.conf](https://invent.kde.org/kde-linux/kde-linux/-/blob/master/mkosi.extra/usr/lib/systemd/logind.conf.d/00-kde-linux-killuserprocesses.conf)

- [x] **No shutdown wall messages** — no broadcast text on the console at shutdown.
  [disable-wall.conf](https://invent.kde.org/kde-linux/kde-linux/-/blob/master/mkosi.extra/etc/systemd/logind.conf.d/disable-wall.conf)

- [x] **Baloo skips `~/.snapshots`** — file indexing does not walk Snapper snapshots.
  [baloofilerc](https://invent.kde.org/kde-linux/kde-linux/-/blob/master/mkosi.extra/etc/xdg/baloofilerc)

- [x] **Quieter Qt logging** — debug and info messages stay out of the journal.
  [qtlogging.ini](https://invent.kde.org/kde-linux/kde-linux/-/blob/master/mkosi.extra/etc/xdg/QtProject/qtlogging.ini)

- [x] **Qt texture workaround** — fewer QtMultimedia glitches on AMD GPUs.
  [50-kde-linux-workarounds.conf](https://invent.kde.org/kde-linux/kde-linux/-/blob/master/mkosi.extra/usr/lib/environment.d/50-kde-linux-workarounds.conf)

- [x] **ksshaskpass** — Dolphin and git ask for SSH passwords in a window.
  [60-ssh-askpass.conf](https://invent.kde.org/kde-linux/kde-linux/-/blob/master/mkosi.extra/usr/lib/environment.d/60-ssh-askpass.conf)

- [x] **SSH agent socket** — graphical apps see the systemd ssh-agent.
  [60-ssh-auth-sock.conf](https://invent.kde.org/kde-linux/kde-linux/-/blob/master/mkosi.extra/usr/lib/environment.d/60-ssh-auth-sock.conf)

- [x] **Wi-Fi country from the timezone** — uses the legal channels for that country, so more of them work.
  [iw-set-regdomain](https://invent.kde.org/kde-linux/kde-linux/-/blob/master/mkosi.extra/usr/lib/iw-set-regdomain),
  [85-iw-regulatory.rules](https://invent.kde.org/kde-linux/kde-linux/-/blob/master/mkosi.extra/usr/lib/udev/rules.d/85-iw-regulatory.rules),
  [kde-linux-iw-set-regdomain.service](https://invent.kde.org/kde-linux/kde-linux/-/blob/master/mkosi.extra/usr/lib/systemd/system/kde-linux-iw-set-regdomain.service),
  [kde-linux-iw-set-regdomain.path](https://invent.kde.org/kde-linux/kde-linux/-/blob/master/mkosi.extra/usr/lib/systemd/system/kde-linux-iw-set-regdomain.path)

- [x] **Network hardening sysctl** — blocks address spoofing and ICMP redirects, uses temporary IPv6 addresses, and stops unprivileged BPF. The same file also disables kexec.
  [55-hardening.conf](https://invent.kde.org/kde-linux/kde-linux/-/blob/master/mkosi.extra/usr/lib/sysctl.d/55-hardening.conf)

- [x] **thermald** — keeps Intel CPUs from hitting the thermal limit as hard.
  [00-kde-linux.preset](https://invent.kde.org/kde-linux/kde-linux/-/blob/master/mkosi.extra/usr/lib/systemd/system-preset/00-kde-linux.preset) (`enable thermald.service`)

- [x] **Geoclue via BeaconDB** — location for Night Color without the built-in static source.
  [00-kde-linux-geoclue-default.conf](https://invent.kde.org/kde-linux/kde-linux/-/blob/master/mkosi.extra/etc/geoclue/conf.d/00-kde-linux-geoclue-default.conf)

- [x] **Firefox Flatpak setup** — GTK accent color, speech dispatcher, the Plasma browser extension, and NVIDIA video decode.
  [configure-firefox](https://invent.kde.org/kde-linux/kde-linux/-/blob/master/mkosi.extra/usr/lib/configure-firefox),
  [00-kde-linux-default.js](https://invent.kde.org/kde-linux/kde-linux/-/blob/master/mkosi.extra/usr/share/firefox-config/00-kde-linux-default.js),
  [01-kde-linux-nvidia-hw-decode.js](https://invent.kde.org/kde-linux/kde-linux/-/blob/master/mkosi.extra/usr/share/firefox-config/01-kde-linux-nvidia-hw-decode.js),
  [policies.json](https://invent.kde.org/kde-linux/kde-linux/-/blob/master/mkosi.extra/usr/share/firefox-config/policies.json),
  [kde-linux-configure-firefox.service](https://invent.kde.org/kde-linux/kde-linux/-/blob/master/mkosi.extra/usr/lib/systemd/system/kde-linux-configure-firefox.service),
  [kde-linux-configure-firefox.path](https://invent.kde.org/kde-linux/kde-linux/-/blob/master/mkosi.extra/usr/lib/systemd/system/kde-linux-configure-firefox.path)

- [x] **NVIDIA power tweaks** — the GPU can sleep, suspend does not freeze the session, and GPU memory allocation skips the security wipe so it is faster.
  [80-nvidia-power-management.rules](https://invent.kde.org/kde-linux/kde-linux/-/blob/master/mkosi.extra/usr/lib/udev/rules.d/80-nvidia-power-management.rules),
  [99-nvidia-no-freeze-session.rules](https://invent.kde.org/kde-linux/kde-linux/-/blob/master/mkosi.extra/usr/lib/udev/rules.d/99-nvidia-no-freeze-session.rules),
  [10-nvidia-no-freeze-session.conf](https://invent.kde.org/kde-linux/kde-linux/-/blob/master/mkosi.extra/usr/lib/systemd/system/systemd-suspend.service.d/10-nvidia-no-freeze-session.conf),
  [00-nvidia.conf](https://invent.kde.org/kde-linux/kde-linux/-/blob/master/mkosi.extra/usr/lib/modprobe.d/00-nvidia.conf)

# Open issues

From the testing laptop. The fixes below are in the tree. Nothing was
submitted, nothing was installed on the Tumbleweed host, and no ISO
was built. That machine has this OS and Grok, so the next pass can
check the installed result. The initrd on the laptop does not change
until the next initrd rebuild, so the Plymouth field appears after that.

- [x] **TPM password prompt** — measured boot does not always unlock.
  When a passphrase is needed, Plymouth shows no password field. The
  passphrase has to be typed on a TTY. The theme now draws "Disk
  passphrase" and a cursor in the middle of the screen, including
  when no bullets have been typed. The artwork is unchanged.

- [x] **plasmalogin theme** — `/etc/plasmalogin.conf` sets the greeter
  wallpaper plugin to `org.kde.image` and the image to
  `file:///usr/share/wallpapers/atropos-02`. There is no separate
  Aether greeter theme. `org.kde.image` is on the greeter allow list.

- [x] **ISO grub theme** — `grub2-branding-atropos` is the live menu.
  The openSUSE GRUB theme has no background image, so this one does
  not either. The desktop color stays `#0D202F`. The green selection
  bar and scrollbar are rose `#c7080b`. Kiwi's `bootloader-theme` is
  `atropos`. `fix_bootconfig` was still selecting `openSUSE`, so the
  menu stayed unthemed. It now prefers `themes/atropos`.
  `grub2-branding-openSUSE` is not installed: it conflicts with
  `grub2-branding`. The installed system stays on systemd-boot.
  The ISO itself was not rebuilt.

- [x] **Aether red glow** — the highlight glow had been recolored to
  the breeze blue accent. It is rose `199,8,11` again. The original
  Aether glow gradient stays in the theme.

- [x] **AetherDark colors** — the scheme is openSUSE Dark with rose
  accent `199,8,11`, including the inactive header. Views stay dark.
  The name stays AetherDark. `kdeglobals` in this repository is still
  only the laptop reference.

- [x] **Flatpak first-login notice** — Welcome Center loads
  `01-flatpaks.qml`, which says the Flatpak applications are still
  downloading. The page does not start the download.

- [x] **tuned-ppd** — KDE Linux runs the tuned daemon and tuned-ppd.
  AtropOS preset only `tuned-ppd.service`. That unit `Requires=tuned.service`,
  so the power-profiles bus had nothing to start when the daemon was
  left disabled. Both units now have static wants links, and the
  preset enables both.

- [ ] **Discover / PackageKit** — still open. See `discoverlog.txt`
  and `discoverlog (1).txt`. PackageKit stops, the transaction error
  is empty, and `org.freedesktop.PackageKit.Offline` `PreparedUpgrade`
  cannot be invalidated. The gdb session was downloading debuginfo
  when PackageKit died. The backend no longer sends a blank error of
  its own: an empty command failure names the exit code. The offline
  stub stays. The invalidation lines are packagekit-qt after
  packagekitd leaves the bus. PackageKit itself is not patched.

- [x] **Cover Switch** — `LayoutName=coverswitch` is set for new
  accounts and in the global theme. An existing account is switched
  once, and only when the saved layout is empty or `thumbnail_grid`.
  A layout the user already chose is left alone.

- [x] **McFly** — the hook runs only in an interactive shell. It
  creates a missing history file and skips `mcfly init` when that
  file still cannot be read.

- [x] **atychia user unit** — atychia does not work because
  `atychiad.service` did not run. `systemctl --user` on the absolute
  path `/usr/lib/systemd/user/atychiad.service` looks up
  `usr-lib-systemd-user-atychiad.service.mount`, and that unit does
  not exist. The package now enables `atychiad.service` by unit name
  with a static relative symlink in `default.target.wants`.
