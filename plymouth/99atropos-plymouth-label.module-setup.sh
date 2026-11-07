#!/bin/bash

# Plymouth draws Image.Text through label-pango.so. plymouth-populate-initrd
# copies label.so and, when present, label-freetype.so. Slowroll ships
# neither, so the passphrase callback runs and the splash stays blank.
#
# Every failure below is a warning. "exit 1" from install() aborts the
# whole dracut run, so the kernel update fails and transactional-update
# discards the snapshot. Without the label the splash only loses its text.

check() {
    [[ $mount_needs ]] && return 1
    local plugin
    for plugin in \
        "${dracutsysrootdir-}/usr/lib64/plymouth/label-pango.so" \
        "${dracutsysrootdir-}/usr/lib/plymouth/label-pango.so"
    do
        [[ -e $plugin ]] && return 0
    done
    return 1
}

depends() {
    echo plymouth
}

install() {
    local plugin font family conf
    for plugin in /usr/lib64/plymouth/label-pango.so /usr/lib/plymouth/label-pango.so; do
        if [[ -e ${dracutsysrootdir-}$plugin ]]; then
            inst "$plugin" || {
                dwarn "atropos-plymouth-label: could not install $plugin"
                return 0
            }
            break
        fi
    done

    # libthai opens this dictionary while pango shapes a label.
    if [[ -e ${dracutsysrootdir-}/usr/share/libthai/thbrk.tri ]]; then
        inst /usr/share/libthai/thbrk.tri || \
            dwarn "atropos-plymouth-label: could not install thbrk.tri"
    fi

    if ! command -v fc-match > /dev/null || ! command -v fc-query > /dev/null; then
        dwarn "atropos-plymouth-label: fc-match is missing, the splash text needs a Sans font"
        return 0
    fi
    font=$(fc-match -f '%{file}' Sans)
    if [[ ! -f $font ]]; then
        dwarn "atropos-plymouth-label: no Sans font, the splash has no text"
        return 0
    fi
    family=$(fc-query -f '%{family}' "$font" | cut -d, -f1)
    [[ -n $family ]] || family="sans-serif"
    family=${family//&/&amp;}
    family=${family//</&lt;}
    family=${family//>/&gt;}
    family=${family//\"/&quot;}
    inst "$font" || {
        dwarn "atropos-plymouth-label: could not install $font"
        return 0
    }

    # The host conf.d aliases families whose fonts are not in the initrd.
    # "Sans 22" from the theme has to resolve to the one font copied above.
    conf=$(mktemp) || {
        dwarn "atropos-plymouth-label: could not create fonts.conf"
        return 0
    }
    cat > "$conf" << EOF
<?xml version="1.0"?>
<!DOCTYPE fontconfig SYSTEM "urn:fontconfig:fonts.dtd">
<fontconfig>
  <dir>/usr/share/fonts</dir>
  <alias>
    <family>Sans</family>
    <prefer><family>${family}</family></prefer>
  </alias>
  <alias>
    <family>sans-serif</family>
    <prefer><family>${family}</family></prefer>
  </alias>
  <alias>
    <family>sans</family>
    <prefer><family>${family}</family></prefer>
  </alias>
</fontconfig>
EOF
    # mktemp is mode 0600. plymouthd is root, and a normal fonts.conf is 0644.
    chmod 644 "$conf"
    inst_simple "$conf" /etc/fonts/fonts.conf || \
        dwarn "atropos-plymouth-label: could not install fonts.conf"
    rm -f "$conf"
}
