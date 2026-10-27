# Sourced from /etc/profile.d. csh counterpart of atropos-applications.sh.
if ( ! ${?XDG_DATA_DIRS} ) then
    setenv XDG_DATA_DIRS /usr/share/atropos:/usr/local/share:/usr/share
else
    if ( ":${XDG_DATA_DIRS}:" !~ *:/usr/share/atropos:* ) then
        setenv XDG_DATA_DIRS "/usr/share/atropos:${XDG_DATA_DIRS}"
    endif
endif
