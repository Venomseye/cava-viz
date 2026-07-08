_viz_completions() {
    local cur prev words cword
    _init_completion || return

    local formats='plain waybar polybar eww raw dzen2 i3bar'
    local backends='auto pipewire pulse'
    local sinks='stdout fifo socket'
    local stereo_modes='merge split'

    case "$prev" in
        -b|--backend)
            COMPREPLY=( $(compgen -W "$backends" -- "$cur") )
            return ;;
        -s|--source)
            # Offer known sources from pactl if available
            local sources
            sources=$(pactl list short sources 2>/dev/null | awk '{print $2}')
            COMPREPLY=( $(compgen -W "$sources" -- "$cur") )
            return ;;
        -r|--rate)
            COMPREPLY=( $(compgen -W "44100 48000 88200 96000" -- "$cur") )
            return ;;
        -t|--theme)
            COMPREPLY=( $(compgen -W "0 1 2 3 4 5 6 7 8 9 10 11" -- "$cur") )
            return ;;
        -f|--fps)
            COMPREPLY=( $(compgen -W "15 30 60 120" -- "$cur") )
            return ;;
        --bar-format)
            COMPREPLY=( $(compgen -W "$formats" -- "$cur") )
            return ;;
        --bar-count)
            COMPREPLY=( $(compgen -W "5 8 10 12 16 20" -- "$cur") )
            return ;;
        --bar-fps)
            COMPREPLY=( $(compgen -W "10 15 20 30" -- "$cur") )
            return ;;
        --bar-stereo)
            COMPREPLY=( $(compgen -W "$stereo_modes" -- "$cur") )
            return ;;
        --bar-sink)
            COMPREPLY=( $(compgen -W "$sinks" -- "$cur") )
            return ;;
        --bar-out)
            _filedir
            return ;;
        --bar-color)
            # No completion — free-form #RRGGBB
            return ;;
        --bar-chars|--bar-sep)
            # No completion — free-form strings
            return ;;
    esac

    # Long and short options
    local opts='
        -b --backend
        -s --source
        -M --mic
        -r --rate
        -t --theme
        -f --fps
        -w --autowidth
        --list-sources
        --check
        -V --version
        -h --help
        --bar
        --bar-format
        --bar-count
        --bar-chars
        --bar-color
        --bar-fps
        --bar-stereo
        --bar-sep
        --bar-sink
        --bar-out
    '
    COMPREPLY=( $(compgen -W "$opts" -- "$cur") )
}

complete -F _viz_completions viz
