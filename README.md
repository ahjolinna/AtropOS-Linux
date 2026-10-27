# AtropOS

![AtropOS](products/AtropOS.svg)

**Powered by openSUSE • Sealed by AtropOS**

A rolling immutable KDE Plasma desktop. The system updates in one snapshot. Applications come from Flatpak.

## The desktop

- Aether Dark, Kora icons, the Adobe Source fonts, and the AtropOS splash
- AtropOS wallpapers, with AtropOS 02 on the desktop and a silent looping video
- Cover Switch, a quiet boot, and the AtropOS Plymouth theme
- Firefox and the daily KDE applications, installed from Flathub on first boot

## The system

- openSUSE Slowroll, `kernel-longterm`, and systemd-boot
- A read-only Btrfs root, snapshots, and `transactional-update`
- SELinux enforcing, measured boot, and TPM disk unlock
- An Agama installer, online or with the packages carried on the disc
- An optional open NVIDIA driver for the longterm kernel

## Source

[github.com/ahjolinna/AtropOS-Linux](https://github.com/ahjolinna/AtropOS-Linux)

Each directory is one package on the Open Build Service, project `home:ahjolinna`.

```sh
osc build
```

The live image is `installer/`.

## Packages

| Directory | Package | Role |
| --- | --- | --- |
| `products/` | `agama-products-atropos` | Agama product definition |
| `patterns/` | `patterns-atropos` | Base desktop, and optional NVIDIA |
| `release/` | `AtropOS-release` | `os-release` and the operating-system metadata |
| `aether/` | `plasma6-theme-aether-dark` | Aether Dark |
| `wallpapers/` | `atropos-wallpapers` | Still wallpapers and the video wallpaper |
| `splash/` | `plasma6-lookandfeel-atropos` | Login splash |
| `plymouth/` | `plymouth-theme-atropos` | Boot splash |
| `grub/` | `grub2-branding-atropos` | Live ISO boot theme |
| `performance/` | `atropos-performance` | Desktop defaults, silent boot, and the Flatpak seed |
| `nvidia/` | `atropos-nvidia` | First-boot open driver |
| `installer/` | `agama-installer-atropos` | Offline and Online live ISOs |
