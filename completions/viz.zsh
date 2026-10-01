#compdef viz

_viz_sources() {
    local -a sources
    sources=( ${(f)"$(pactl list short sources 2>/dev/null | awk '{print $2}')"} )
    _describe 'audio source' sources
}

_viz() {
    _arguments -s \
        '(-b --backend)'{-b,--backend}'[Audio backend]:backend:(auto pipewire pulse)' \
        '(-s --source)'{-s,--source}'[Capture device (reconnects to it if it drops)]:source:_viz_sources' \
        '(-M --mic)'{-M,--mic}'[Capture from the default microphone]' \
        '(-r --rate)'{-r,--rate}'[Sample rate in Hz (8000-192000)]:rate:(44100 48000 88200 96000)' \
        '(-t --theme)'{-t,--theme}'[Starting theme index, this session only (0-11 built-in, 12+ user)]:index:(0 1 2 3 4 5 6 7 8 9 10 11)' \
        '(-f --fps)'{-f,--fps}'[Target FPS, this session only]:fps:(15 30 60 120)' \
        '(-w --autowidth)'{-w,--autowidth}'[Auto bar width to fill terminal]' \
        '--list-sources[List available audio sources and exit]' \
        '--check[Validate config and exit]' \
        '(-V --version)'{-V,--version}'[Print version and exit]' \
        '(-h --help)'{-h,--help}'[Print help and exit]'
}

_viz "$@"
