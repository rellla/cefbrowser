// OIPF DAE 7.16.5: extensions for playback of selected media components
// (audio and subtitle track selection) for the A/V object and video/broadcast,
// and the HTML5 audioTracks/textTracks used by HbbTV 2 applications.
//
// The track index is the position in the PID ordered track list. VDR uses the
// same order for the selection (cefSelectAudioTrack, cefSelectSubtitleTrack).

const COMPONENT_TYPE_VIDEO = 0;
const COMPONENT_TYPE_AUDIO = 1;
const COMPONENT_TYPE_SUBTITLE = 2;

function _vdrCollection(list) {
    let collection = list.slice();
    collection.item = (i) => collection[i] || null;
    return collection;
}

// tracks of a broadband video (from the transcoder) or of the current channel (live TV)
function _vdrReadTracks(isBroadcast) {
    let result = { audio: [], subtitle: [] };

    try {
        if (isBroadcast) {
            let channel = window.HBBTV_POLYFILL_NS.currentChannel;
            if (channel && channel.components) {
                result.audio = channel.components.audio || [];
                result.subtitle = channel.components.subtitle || [];
            }
        } else if (window.cefAudioInfo) {
            let info = JSON.parse(window.cefAudioInfo() || "{}");
            result.audio = info.stream || [];
            result.subtitle = info.subtitle || [];
        }
    } catch (e) {
        console.log("avcomponents: unable to read the tracks: " + e);
    }

    return result;
}

function _vdrEncoding(codec) {
    switch (codec) {
        case "ac3": return "audio/ac3";
        case "eac3": return "audio/eac3";
        case "aac": return "audio/mp4";
        case "mp2":
        case "mp3": return "audio/mpeg";
        default: return codec || "";
    }
}

window.addComponentFunctions = function (node, isBroadcast) {
    if (node._vdrComponents) {
        return;
    }
    node._vdrComponents = true;

    node.COMPONENT_TYPE_VIDEO = COMPONENT_TYPE_VIDEO;
    node.COMPONENT_TYPE_AUDIO = COMPONENT_TYPE_AUDIO;
    node.COMPONENT_TYPE_SUBTITLE = COMPONENT_TYPE_SUBTITLE;

    // the first audio track is active, subtitles are off
    let activeAudio = 0;
    let activeSubtitle = -1;

    function components(type) {
        let tracks = _vdrReadTracks(isBroadcast);
        let list = [];

        if (type === undefined || type === null || type === COMPONENT_TYPE_VIDEO) {
            list.push({
                type: COMPONENT_TYPE_VIDEO,
                componentTag: 0,
                pid: 0,
                encoding: "video/mpeg",
                encrypted: false,
                aspectRatio: 1.78,
                _index: 0
            });
        }

        if (type === undefined || type === null || type === COMPONENT_TYPE_AUDIO) {
            tracks.audio.forEach((a, i) => {
                list.push({
                    type: COMPONENT_TYPE_AUDIO,
                    componentTag: i,
                    pid: a.pid || 0,
                    encoding: a.encoding || _vdrEncoding(a.codec),
                    encrypted: false,
                    language: a.language || "",
                    audioDescription: !!a.audioDescription,
                    audioChannels: a.channels || 2,
                    _index: i
                });
            });
        }

        if (type === undefined || type === null || type === COMPONENT_TYPE_SUBTITLE) {
            tracks.subtitle.forEach((s, i) => {
                list.push({
                    type: COMPONENT_TYPE_SUBTITLE,
                    componentTag: i,
                    pid: s.pid || 0,
                    encoding: "DVB-SUBT",
                    encrypted: false,
                    language: s.language || "",
                    hearingImpaired: !!s.hearingImpaired,
                    _index: i
                });
            });
        }

        return list;
    }

    function notify(type) {
        if (typeof node.onSelectedComponentChanged === 'function') {
            try {
                node.onSelectedComponentChanged(type);
            } catch (e) {
                console.error(e);
            }
        }

        let event = new Event('SelectedComponentChanged');
        event.componentType = type;
        node.dispatchEvent(event);
    }

    node.getComponents = function (type) {
        return _vdrCollection(components(type));
    };

    node.getCurrentActiveComponents = function (type) {
        return _vdrCollection(components(type).filter((c) =>
            c.type === COMPONENT_TYPE_VIDEO ||
            (c.type === COMPONENT_TYPE_AUDIO && c._index === activeAudio) ||
            (c.type === COMPONENT_TYPE_SUBTITLE && c._index === activeSubtitle)));
    };

    node.selectComponent = function (component) {
        // a component type selects the default track of this type
        if (typeof component === 'number') {
            component = { type: component, _index: 0 };
        }

        if (!component) {
            return;
        }

        // components of the application are found by pid or language
        let index = component._index;
        if (index === undefined) {
            let list = components(component.type);
            let found = list.find((c) => (component.pid && c.pid === component.pid) ||
                                         (component.language && c.language === component.language));
            index = found ? found._index : -1;
        }

        if (component.type === COMPONENT_TYPE_AUDIO && index >= 0) {
            if (window.cefSelectAudioTrack && window.cefSelectAudioTrack(index)) {
                activeAudio = index;
                notify(COMPONENT_TYPE_AUDIO);
            }
        } else if (component.type === COMPONENT_TYPE_SUBTITLE && index >= 0) {
            if (window.cefSelectSubtitleTrack && window.cefSelectSubtitleTrack(index)) {
                activeSubtitle = index;
                notify(COMPONENT_TYPE_SUBTITLE);
            }
        }
    };

    node.unselectComponent = function (component) {
        let type = typeof component === 'number' ? component : (component ? component.type : undefined);

        if (type === COMPONENT_TYPE_SUBTITLE) {
            if (window.cefSelectSubtitleTrack && window.cefSelectSubtitleTrack(-1)) {
                activeSubtitle = -1;
                notify(COMPONENT_TYPE_SUBTITLE);
            }
        } else if (type === COMPONENT_TYPE_AUDIO) {
            // audio can't be switched off, return to the default track
            node.selectComponent({ type: COMPONENT_TYPE_AUDIO, _index: 0 });
        }
    };
};

// HTML5 media element (HbbTV 2): audioTracks and the subtitle tracks in textTracks.
// Chromium offers audioTracks only behind a flag.
window.addHtml5TrackFunctions = function (video) {
    if (video._vdrTracks) {
        return;
    }
    video._vdrTracks = true;

    let audioTracks = null;

    function buildAudioTracks() {
        let tracks = _vdrReadTracks(false).audio;
        let list = [];

        let listeners = [];
        list.onchange = null;
        list.addEventListener = (type, l) => { if (type === 'change') listeners.push(l); };
        list.removeEventListener = (type, l) => { listeners = listeners.filter((x) => x !== l); };
        list.getTrackById = (id) => list.find((t) => t.id === id) || null;

        function changed() {
            let event = new Event('change');
            if (typeof list.onchange === 'function') {
                list.onchange(event);
            }
            listeners.forEach((l) => l(event));
        }

        tracks.forEach((a, i) => {
            let enabled = i === 0;
            let track = {
                id: String(i),
                kind: a.audioDescription ? "description" : (i === 0 ? "main" : "translation"),
                label: a.comment || a.language || ("Audio " + (i + 1)),
                language: a.language || ""
            };

            Object.defineProperty(track, 'enabled', {
                get: () => enabled,
                set: (value) => {
                    if (value && !enabled && window.cefSelectAudioTrack && window.cefSelectAudioTrack(i)) {
                        list.forEach((t) => t._setEnabled(false));
                        enabled = true;
                        changed();
                    }
                }
            });
            track._setEnabled = (value) => { enabled = value; };

            list.push(track);
        });

        return list;
    }

    if (!('audioTracks' in video) || video.audioTracks === undefined) {
        Object.defineProperty(video, 'audioTracks', {
            configurable: true,
            get: () => {
                if (audioTracks === null || audioTracks.length === 0) {
                    audioTracks = buildAudioTracks();
                }
                return audioTracks;
            }
        });
    }

    // a new video has new tracks
    video.addEventListener('loadstart', () => { audioTracks = null; });
};
