#compdef viz

_viz_sources() {
    local sources
    sources=( ${(f)"$(pactl list short sources 2>/dev/null | awk '{print $2}')"} )
    _describe 'audio source' sources
}

_viz() {
    local -a formats sinks stereo_modes backends

    formats=(
        'plain:Raw UTF-8 block chars (eww, scripts)'
        'waybar:JSON with pango span colour tags'
        'polybar:Polybar / lemonbar %{F#hex} tags'
        'eww:Same as plain (eww uses CSS for colour)'
        'raw:Space-separated floats 0.0-1.0'
        'dzen2:dzen2 ^fg(#hex) colour tags'
        'i3bar:i3bar JSON full_text block'
    )

    backends=(
        'auto:Auto-detect (PipeWire first, then PulseAudio)'
        'pipewire:Force PipeWire backend'
        'pulse:Force PulseAudio backend'
    )

    sinks=(
        'stdout:Write to standard output (default)'
        'fifo:Write to a named pipe at --bar-out path'
        'socket:Unix socket server at --bar-out path'
    )

    stereo_modes=(
        'merge:Average L+R channels into one string (default)'
        'split:Emit L | R as two separate bar strings'
    )

    _arguments -s \
        '(-b --backend)'{-b,--backend}'[Audio backend]:backend:( '"${backends[@]%:*}"' )' \
        '(-s --source)'{-s,--source}'[Capture device]:source:_viz_sources' \
        '(-M --mic)'{-M,--mic}'[Capture from microphone]' \
        '(-r --rate)'{-r,--rate}'[Sample rate in Hz]:rate:(44100 48000 88200 96000)' \
        '(-t --theme)'{-t,--theme}'[Starting theme index (0-11 built-in, 12+ user)]:index:(0 1 2 3 4 5 6 7 8 9 10 11)' \
        '(-f --fps)'{-f,--fps}'[Target FPS]:fps:(15 30 60 120)' \
        '(-w --autowidth)'{-w,--autowidth}'[Auto bar width to fill terminal]' \
        '--list-sources[List available audio sources and exit]' \
        '--check[Validate config and exit]' \
        '(-V --version)'{-V,--version}'[Print version and exit]' \
        '(-h --help)'{-h,--help}'[Print help and exit]' \
        '--bar[Enable headless bar mode]' \
        '--bar-format[Output format]:format:->bar_format' \
        '--bar-count[Bars per channel]:count:(5 8 10 12 16 20)' \
        '--bar-chars[Level characters]:chars:' \
        '--bar-color[Accent color (#RRGGBB)]:color:' \
        '--bar-fps[Output frame rate]:fps:(10 15 20 30)' \
        '--bar-stereo[Stereo handling]:mode:->bar_stereo' \
        '--bar-sep[Stereo split separator]:string:' \
        '--bar-sink[Output sink type]:sink:->bar_sink' \
        '--bar-out[FIFO or socket path]:path:_files'

    case "$state" in
        bar_format) _describe 'output format' formats ;;
        bar_stereo)  _describe 'stereo mode'   stereo_modes ;;
        bar_sink)    _describe 'sink type'     sinks ;;
    esac
}

_viz "$@"
