# shellcheck shell=sh
# Sourced from /etc/profile for login shells outside the Plasma session
# (TTY, SSH), which do not read environment.d. An EDITOR set by the user
# is kept.
if [ -z "${EDITOR:-}" ] && [ -x /usr/bin/nano ]; then
    EDITOR=nano
    export EDITOR
fi
