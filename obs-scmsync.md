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
| zsh-autosuggestions | `https://github.com/ahjolinna/AtropOS-Linux.git?subdir=zsh-autosuggestions#main` |
| zsh-syntax-highlighting | `https://github.com/ahjolinna/AtropOS-Linux.git?subdir=zsh-syntax-highlighting#main` |
| zsh-history-substring-search | `https://github.com/ahjolinna/AtropOS-Linux.git?subdir=zsh-history-substring-search#main` |
| zsh-completions | `https://github.com/ahjolinna/AtropOS-Linux.git?subdir=zsh-completions#main` |

Every package archive in this repository is in git, except
`plasma6-login-manager`, which fetches its archive from the `_service`.

## GitHub webhooks

OBS does not poll git. One OBS token, bound to one package in
`home:ahjolinna`, needs one webhook on
https://github.com/ahjolinna/AtropOS-Linux
(Settings, Webhooks, Add webhook). The 19 packages above need 19 webhooks.
A push updates only the package whose token is in that webhook.

The numeric id and the secret come from `osc token`. Do not put the
secret in the URL, and do not use a GitHub token as the secret.

| Field | Value |
|---|---|
| Payload URL | `https://build.opensuse.org/trigger/webhook?id=TOKEN_ID` |
| Content type | `application/x-www-form-urlencoded` |
| Secret | that token's OBS secret string |
| SSL verification | on |
| Event | Just the push event |

`https://build.opensuse.org/trigger/workflow?id=` is the workflow
endpoint. It needs a workflow token and `.obs/workflows.yml`. This
repository has no workflow file, so that URL does not refresh a
scmsync package.

If GitHub shows the delivery and the package has no `_scmsync` update,
run `osc service remoterun home:ahjolinna PACKAGE` once for that package.
