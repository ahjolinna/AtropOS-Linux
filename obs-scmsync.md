# OBS SCM Sync URLs

Set this on each package in `home:ahjolinna`. Do not set it on the project.
A project URL would also turn `docs`, `screenshots`, and `scripts` into packages.

The branch is `main`. `subdir` is the directory in
https://github.com/ahjolinna/AtropOS-Linux

| Package | SCM Sync URL |
|---|---|
| plasma6-login-manager | `https://github.com/ahjolinna/AtropOS-Linux.git?subdir=plasma6-login-manager#main` |
| patterns-atropos | `https://github.com/ahjolinna/AtropOS-Linux.git?subdir=patterns#main` |
| atropos-performance | `https://github.com/ahjolinna/AtropOS-Linux.git?subdir=performance#main` |
| atropos-check | `https://github.com/ahjolinna/AtropOS-Linux.git?subdir=check#main` |
| atropos-nvidia | `https://github.com/ahjolinna/AtropOS-Linux.git?subdir=nvidia#main` |
| PackageKit-backend-atropos | `https://github.com/ahjolinna/AtropOS-Linux.git?subdir=packagekit#main` |
| AtropOS-release | `https://github.com/ahjolinna/AtropOS-Linux.git?subdir=release#main` |
| grub2-branding-atropos | `https://github.com/ahjolinna/AtropOS-Linux.git?subdir=grub#main` |
| agama-products-atropos | `https://github.com/ahjolinna/AtropOS-Linux.git?subdir=products#main` |
| agama-installer-atropos | `https://github.com/ahjolinna/AtropOS-Linux.git?subdir=installer#main` |
| atychia | `https://github.com/ahjolinna/AtropOS-Linux.git?subdir=atychia#main` |
| plymouth-theme-atropos | `https://github.com/ahjolinna/AtropOS-Linux.git?subdir=plymouth#main` |
| plasma6-lookandfeel-atropos | `https://github.com/ahjolinna/AtropOS-Linux.git?subdir=splash#main` |
| plasma6-theme-aether-dark | `https://github.com/ahjolinna/AtropOS-Linux.git?subdir=aether#main` |
| atropos-wallpapers | `https://github.com/ahjolinna/AtropOS-Linux.git?subdir=wallpapers#main` |

Every package archive in this repository is in git, except
`plasma6-login-manager`, which fetches its archive from the `_service`.
