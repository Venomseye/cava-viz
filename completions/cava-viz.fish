# Fish completions for viz (cava-viz)
# Place in ~/.config/fish/completions/viz.fish
# or /usr/share/fish/vendor_completions.d/viz.fish

# Disable file completion by default
complete -c viz -f

# ── Audio source helper ───────────────────────────────────────────────────────
function __viz_sources
    pactl list short sources 2>/dev/null | awk '{print $2}'
end

# ── General flags ─────────────────────────────────────────────────────────────
complete -c viz -s b -l backend      -d 'Audio backend'              -r -a 'auto pipewire pulse'
complete -c viz -s s -l source       -d 'Capture device'             -r -a '(__viz_sources)'
complete -c viz -s M -l mic          -d 'Use microphone input'
complete -c viz -s r -l rate         -d 'Sample rate in Hz'          -r -a '44100 48000 88200 96000'
complete -c viz -s t -l theme        -d 'Theme index (0-11 built-in, 12+ user)' -r -a '0 1 2 3 4 5 6 7 8 9 10 11'
complete -c viz -s f -l fps          -d 'Target FPS'                 -r -a '15 30 60 120'
complete -c viz -s w -l autowidth    -d 'Auto bar width to fill terminal'
complete -c viz      -l list-sources -d 'List available audio sources and exit'
complete -c viz      -l check        -d 'Validate config and exit'
complete -c viz -s V -l version      -d 'Print version and exit'
complete -c viz -s h -l help         -d 'Print help and exit'

# ── Bar mode ──────────────────────────────────────────────────────────────────
complete -c viz      -l bar          -d 'Enable headless bar mode'

complete -c viz      -l bar-format   -d 'Output format'              -r \
    -a 'plain\t"Raw UTF-8 block chars"
        waybar\t"JSON with pango span tags (Waybar)"
        polybar\t"Polybar/lemonbar colour tags"
        eww\t"Plain text (eww uses CSS)"
        raw\t"Space-separated floats 0.0-1.0"
        dzen2\t"dzen2 ^fg() colour tags"
        i3bar\t"i3bar JSON block"'

complete -c viz      -l bar-count    -d 'Bars per channel'           -r -a '5 8 10 12 16 20'
complete -c viz      -l bar-chars    -d 'Level characters (UTF-8)'   -r
complete -c viz      -l bar-color    -d 'Accent color (#RRGGBB)'     -r
complete -c viz      -l bar-fps      -d 'Output frame rate'          -r -a '10 15 20 30'

complete -c viz      -l bar-stereo   -d 'Stereo handling'            -r \
    -a 'merge\t"Average L+R (default)"
        split\t"Emit L | R separately"'

complete -c viz      -l bar-sep      -d 'Stereo split separator'     -r
complete -c viz      -l bar-sink     -d 'Output sink type'           -r \
    -a 'stdout\t"Standard output (default)"
        fifo\t"Named pipe (requires --bar-out)"
        socket\t"Unix socket server (requires --bar-out)"'

complete -c viz      -l bar-out      -d 'FIFO or socket path'        -r -F
