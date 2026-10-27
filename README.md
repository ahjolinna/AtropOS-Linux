# AtropOS

Soft-fork of openSUSE Kalpa on Slowroll. AtropOS has its own Agama product,
pattern, release package, and desktop defaults. Agama itself stays upstream.

The old archive at `/home/ahjolinna/Projects/AtropOS` is reference only.

## Decisions

- Packages come from openSUSE Slowroll: oss, non-oss, and
  `update/slowroll/repo/oss`. The installed system keeps
  `openSUSE-repos-Slowroll`.
- The installed kernel is `kernel-longterm`.
- Agama installs systemd-boot on the target (`boot.default_efi_bootloader`).
  The live ISO may still boot with grub2. Snapshots stay registered through
  `sdbootutil`, `sdbootutil-snapper`, and `sdbootutil-dracut-measure-pcr`.
- Base pattern `atropos_base`. Each requirement is listed once. It does not
  pull `microos_*` or the grub2 stack. `atropos_nvidia` is optional.
- One btrfs filesystem on `/`. `/var` stays a subvolume on that filesystem,
  with copy-on-write off, together with `home`, `opt`, `root`, `srv`,
  `usr/local`, `boot/writable`, and the architecture's `boot/grub2` subvolume.
  Agama adds the EFI system partition. Snapshots and `transactional-update`
  supply the read-only snapshot root, `/.snapshots`, and the writable `/etc`
  inside the snapshot. Disk encryption stays an installer choice.
- SELinux is enforcing and targeted, the Aeon and MicroOS configuration.
  The boot line is `security=selinux selinux=1`. `lsm=` stays unset.
  `selinux-policy-targeted` writes `/etc/selinux/config`.
  `selinux-autorelabel` labels the read-only root once.
  `selinux-policy-targeted-gaming` allows Flatpak Steam, Proton, and Wine.
  `sysextmgr` stays out.
- `atropos-performance` carries the KDE Linux settings that Slowroll can use:
  zram sized to RAM with zstd, the matching sysctl values, BBR plus fq,
  higher inotify limits, a 500M journal cap, short unit timeouts, and an
  open memlock limit. It also enables `systemd-oomd` from
  `systemd-experimental` (10s memory-pressure window, 60% on `system.slice`,
  40% on `user.slice`) and turns memory accounting on, which this systemd
  build leaves off. `powertop --auto-tune` runs at boot and again after
  sleep. USB input devices are put back to `on` afterwards, because
  auto-tune suspends them. Version 0.4.0 adds the remaining KDE Linux
  desktop drop-ins: ntsync, BFQ on hard disks, the USB writeback cap,
  HDA power save, the watchdog blacklist, PAM memlock, logout cleanup,
  no shutdown wall, Baloo skipping `~/.snapshots`, quieter Qt logging,
  the AMD texture workaround, ksshaskpass, the ssh-agent socket, the
  Wi-Fi country from the timezone, network hardening, thermald, BeaconDB
  location, and Firefox Flatpak setup.
- The installed boot is silent. `atropos-performance` ships
  `/usr/kernel/cmdline.d/00-atropos-silent.conf`, which `sdbootutil` appends
  after `/etc/kernel/cmdline`. The line is `quiet splash=silent
  plymouth.ignore-serial-consoles loglevel=3 vt.global_cursor_default=0
  nowatchdog systemd.show_status=auto rd.udev.log_level=1 psi=1
  security=selinux selinux=1`. The console
  loglevel stays at 3 and the NMI watchdog stays off. Plymouth is
  `plymouth`, `plymouth-dracut`, `plymouth-branding-openSUSE`, and
  `plymouth-theme-atropos`. The theme package sets `Theme=atropos`. The
  branding package stays, and `bgrt` is the fallback after the theme
  package is removed. The live ISO uses the same command line and the
  `atropos` splash.
- Measured boot is `sdbootutil-dracut-measure-pcr`, `dracut-pcr-signature`,
  `tpm2.0-tools`, `tpm2.0-abrmd`, `tpm2-0-tss`, `shim`, and `mokutil`.
- New accounts get Aether Dark (`aetherdark`), Kora icons (`kora`), the
  Adobe Source fonts (Source Sans 3, Source Serif Pro, Source Code Pro),
  and the AtropOS 02 wallpaper. AtropOS 01, 03, and 04 install with it.
  The Video wallpaper type loops the silent animation.
  `atropos-wallpapers` requires `qt6-multimedia` and
  `qt6-multimedia-imports` so that plugin can play it.
  Papirus stays installed and can be selected in System Settings.
  The login splash is AtropOS (`org.atropos.desktop`). Its background
  is the AtropOS 02 wallpaper. The task switcher is Cover Switch, from
  `kdeplasma6-addons`. Widgets, window decorations, and cursors stay
  Breeze.
  openSUSE Plasma branding is installed, and these skel files keep it
  from selecting the openSUSE theme. Kvantum (`kvantum-qt6`,
  `kvantum-qt5`, `kvantum-manager`, and `kvantum-themes`) is installed
  so a third-party Qt theme can be selected later.
- A fingerprint reader, FIDO security key, or smartcard reader works
  when it is plugged in. The pattern installs `fprintd`, `fprintd-pam`,
  `pam_u2f`, `libfido2-1`, `libfido2-udev`, `pcsc-ccid`, and
  `pcsc-acsccid`. `pcscd.socket` starts the smartcard daemon. A finger
  is enrolled in System Settings and then unlocks the screen. Sign-in
  stays the account password, including when no key is present. Disk
  unlock stays TPM.
- The base pattern also installs `sensors`, `libdisplay-info-tools`
  (`di-edid-decode`), `clinfo`, `inxi`, `conky`, `android-udev-rules`,
  `android-tools`, `ufw` (left disabled), `hunspell`, `kup-backup`,
  `unrar`, `lrzip`, `orca`, `fastfetch`, `htop`, `git`,
  `yubikey-manager`, `flatpak-builder`, `mcfly`, and `plocate`.
  Interactive bash loads mcfly. `plocate-updatedb.timer` is enabled.
- The Agama software list has an unchecked pattern, `atropos_nvidia`. It
  installs `openSUSE-repos-Slowroll-NVIDIA` (the Tumbleweed NVIDIA
  repository, which is what Slowroll uses) and, on first boot, the Leap and
  Tumbleweed open-driver set: `nvidia-open-driver-G07-signed-kmp-meta`,
  `nvidia-open-driver-G07-signed-kmp-longterm`, `nvidia-userspace-meta-G07`,
  `nvidia-video-G07`, and `nvidia-compute-utils-G07`. The same package
  lets the GPU sleep, keeps suspend from freezing the session while the
  nvidia module is loaded, and skips the GPU memory wipe. That transaction is
  a new snapshot, so the driver is active after the next reboot. The NVIDIA
  repository is not an Agama installation repository. Its PCI supplements
  would pull a driver in during the solve, including the proprietary G06
  line the SDB warns about.

On first boot with a network, `atropos-seed-flatpaks` installs Photos
(`org.kde.koko`), Okular, KWrite, MPC-QT (`io.github.mpc_qt.mpc-qt`),
KCalc, KolourPaint, Safe Eyes (`io.github.slgobinath.SafeEyes`), and Firefox
(`org.mozilla.firefox`) from Flathub. Dolphin, Konsole, Spectacle, Partition
Manager, Ark, Krfb, KRDC, and KHelpCenter stay RPMs. Ark is an RPM so
Dolphin can open archives with the system service menus. The skel browser
entry points at the Flatpak.

After a normal shutdown the systemd-boot menu stays hidden. Hold Space
during boot to open it. After a crash it stays on screen for 7 seconds.
`atropos-bootloader` writes that timeout to the EFI variable and leaves
`loader.conf` alone.

KDE Linux Kapsule does not run here. It has no openSUSE package, it needs
Incus, and its own tests target a KDE Linux image. Distrobox is installed
with Podman, so `distrobox create` works without another package.

## Packages

| Directory | Package | Role |
| --- | --- | --- |
| `products/` | `agama-products-atropos` | `/usr/share/agama/products.d/atropos.yaml` |
| `patterns/` | `patterns-atropos` | `atropos_base` and optional `atropos_nvidia` |
| `release/` | `AtropOS-release` | `os-release` and issue text |
| `aether/` | `plasma6-theme-aether-dark` | Aether Dark Plasma 6 theme and the installed-desktop global theme |
| `wallpapers/` | `atropos-wallpapers` | Four still wallpapers, AtropOS 02 as the default, and the video wallpaper plugin |
| `splash/` | `plasma6-lookandfeel-atropos` | AtropOS login splash |
| `performance/` | `atropos-performance` | KDE Linux performance defaults, silent boot, Aether Dark skel, and the Flatpak seed |
| `nvidia/` | `atropos-nvidia` | First-boot open signed G07 driver for `kernel-longterm` |
| `installer/` | `agama-installer-atropos` | Offline and Online Agama live ISOs |

`installer/` is the Agama live image. Offline embeds the OBS image
repository under `/packages`. Online installs from the Slowroll repositories
in `products/atropos.yaml`, including the update repository. The live ISO
boots with grub2 and `kernel-default`. The installed system still uses
systemd-boot and `kernel-longterm`.

An AtropOS overlay repository is still missing from the product file.
Offline carries the packages because it copies the image repository.
Online can install `atropos_base` only after those packages are published
in a repository the product file lists.

## Not carried over from KDE Linux

KDE Linux services that depend on its own image layout (live setup, erofs,
sysupdate, snapper-home) stay there. KDE Linux stages a new `/usr` image with
systemd-sysupdate and reboots when the user asks. AtropOS keeps that reboot
choice and uses `transactional-update dup` instead, because Slowroll is a
package set rather than one image. `thermald` is not enabled.
`systemd-oomd` is the build shipped in `systemd-experimental`, so its
configuration can still change.

## Branding

The Agama product icon is the purple AtropOS crown shipped by
`agama-products-atropos`. systemd presets still come from the MicroOS
branding package. `AtropOS-release` still suggests the MicroOS distributor
logo for `LOGO=distributor-logo`.
`80-atropos.preset` is read first and disables `transactional-update.timer`.
Discover's Update action runs `transactional-update dup` through
`PackageKit-backend-atropos` and does not reboot. Flatpak updates still apply
immediately. A reboot starts the staged snapshot.
openSUSE Plasma branding stays installed. There is no MicroOS Plasma
branding package. `kalpa-themes` is not installed. `AtropOS-release`
replaces `Kalpa-release`. New accounts take Aether Dark, Kora, the
Adobe Source fonts, the AtropOS 02 wallpaper, and the AtropOS
splash from the global theme, so the openSUSE theme is not selected.
The task switcher is Cover Switch. The display manager is plasmalogin.

## Left in the old archive

- Vendored Agama, agama-web-ui, agama-auto, and agama-yast.
- The Agama live-ISO snapshot.
- tik, the Aeon self-installer, and the Yuga branding.
- `sysextmgr`.
- Provides and Obsoletes of `MicroOS-release`.
- The Steam Deck wording and a hicolor distributor-logo package.

## Build

Each directory is one OBS package. From that directory:

```sh
osc build
```

Nothing here is submitted or committed.
