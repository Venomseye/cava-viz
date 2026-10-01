# Fish completions for viz
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
complete -c viz -s t -l theme        -d 'Starting theme, this session only (0-11 built-in, 12+ user)' -r -a '0 1 2 3 4 5 6 7 8 9 10 11'
complete -c viz -s f -l fps          -d 'Target FPS, this session only'  -r -a '15 30 60 120'
complete -c viz -s w -l autowidth    -d 'Auto bar width to fill terminal'
complete -c viz      -l list-sources -d 'List available audio sources and exit'
complete -c viz      -l check        -d 'Validate config and exit'
complete -c viz -s V -l version      -d 'Print version and exit'
complete -c viz -s h -l help         -d 'Print help and exit'

