# Sourced from /etc/profile. Prepend AtropOS desktop overrides for a
# login shell that does not go through systemd's environment.d.
# flatpak.sh is later in this directory and puts its exports in front.
case ":${XDG_DATA_DIRS:-}:" in
    *:/usr/share/atropos:*) ;;
    *)
        if [ -z "${XDG_DATA_DIRS:-}" ]; then
            XDG_DATA_DIRS="/usr/share/atropos:/usr/local/share:/usr/share"
        else
            XDG_DATA_DIRS="/usr/share/atropos:${XDG_DATA_DIRS}"
        fi
        ;;
esac
export XDG_DATA_DIRS
