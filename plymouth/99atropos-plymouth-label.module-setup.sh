#!/bin/bash

# Plymouth draws Image.Text through label-pango.so. plymouth-populate-initrd
# copies label.so and, when present, label-freetype.so. Slowroll ships
# neither, so the passphrase callback runs and the splash stays blank.

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
                dfatal "atropos-plymouth-label: could not install $plugin"
                exit 1
            }
            break
        fi
    done

    # libthai opens this dictionary while pango shapes a label.
    if [[ -e ${dracutsysrootdir-}/usr/share/libthai/thbrk.tri ]]; then
        inst /usr/share/libthai/thbrk.tri || {
            dfatal "atropos-plymouth-label: could not install thbrk.tri"
            exit 1
        }
    fi

    if ! command -v fc-match > /dev/null || ! command -v fc-query > /dev/null; then
        dfatal "atropos-plymouth-label: fc-match is required to put a Sans font in the initrd"
        exit 1
    fi
    font=$(fc-match -f '%{file}' Sans)
    if [[ ! -f $font ]]; then
        dfatal "atropos-plymouth-label: no Sans font"
        exit 1
    fi
    family=$(fc-query -f '%{family}' "$font" | cut -d, -f1)
    [[ -n $family ]] || family="sans-serif"
    family=${family//&/&amp;}
    family=${family//</&lt;}
    family=${family//>/&gt;}
    family=${family//\"/&quot;}
    inst "$font" || {
        dfatal "atropos-plymouth-label: could not install $font"
        exit 1
    }

    # The host conf.d aliases families whose fonts are not in the initrd.
    # "Sans 22" from the theme has to resolve to the one font copied above.
    conf=$(mktemp)
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
    inst_simple "$conf" /etc/fonts/fonts.conf || {
        rm -f "$conf"
        dfatal "atropos-plymouth-label: could not install fonts.conf"
        exit 1
    }
    rm -f "$conf"
}
