// Media Source Extensions for VDR
//
// The browser can't decode the videos of MSE players (dash.js, hls.js, ...). This
// MediaSource replaces the one of the browser: the appended segments (fragmented MP4)
// are posted to the origin of the page, cefbrowser forwards them to the transcoder,
// which remuxes them to TS for VDR. The media element plays a transparent video,
// its time and state are taken from VDR.
(function () {
    'use strict';

    if (window._vdrMse) {
        return;
    }

    const MSE_PATH = '/__vdrmse__/';
    const SUPPORTED_CODECS = /^(avc1|avc3|hev1|hvc1|mp4a|ac-3|ec-3)(\.|$)/i;
    const RANGE_GAP = 0.1;      // ranges closer than this are merged (seconds)
    const RETRY_DELAY = 500;    // the transcoder buffer is full (ms)

    const log = (msg) => console.log('[mse] ' + msg);

    // --- types --------------------------------------------------------------------------

    function isSupportedType(type) {
        if (typeof type !== 'string') {
            return false;
        }

        if (!/^\s*(video|audio)\/mp4\s*(;|$)/i.test(type)) {
            return false;
        }

        const codecs = /codecs\s*=\s*"?([^";]*)"?/i.exec(type);
        if (!codecs) {
            return true;
        }

        return codecs[1].split(',').every((c) => SUPPORTED_CODECS.test(c.trim()));
    }

    // --- time ranges ----------------------------------------------------------------------

    class VdrTimeRanges {
        constructor(ranges) {
            this._r = ranges.map((r) => [r[0], r[1]]);
        }

        get length() {
            return this._r.length;
        }

        _check(i) {
            if (i < 0 || i >= this._r.length) {
                throw new DOMException('Index ' + i + ' out of range', 'IndexSizeError');
            }
        }

        start(i) {
            this._check(i);
            return this._r[i][0];
        }

        end(i) {
            this._check(i);
            return this._r[i][1];
        }
    }

    function addRange(ranges, start, end) {
        if (!(end > start)) {
            return ranges;
        }

        let result = [];
        let s = start, e = end;
        for (const r of ranges) {
            if (r[1] + RANGE_GAP < s || r[0] - RANGE_GAP > e) {
                result.push(r);
            } else {
                s = Math.min(s, r[0]);
                e = Math.max(e, r[1]);
            }
        }
        result.push([s, e]);
        return result.sort((a, b) => a[0] - b[0]);
    }

    function removeRange(ranges, start, end) {
        let result = [];
        for (const r of ranges) {
            if (r[1] <= start || r[0] >= end) {
                result.push(r);
                continue;
            }
            if (r[0] < start) {
                result.push([r[0], start]);
            }
            if (r[1] > end) {
                result.push([end, r[1]]);
            }
        }
        return result;
    }

    function intersectRanges(a, b) {
        let result = [];
        for (const x of a) {
            for (const y of b) {
                const s = Math.max(x[0], y[0]);
                const e = Math.min(x[1], y[1]);
                if (e > s) {
                    result.push([s, e]);
                }
            }
        }
        return result.sort((p, q) => p[0] - q[0]);
    }

    // --- fragmented MP4: only the times of the segments ----------------------------------

    function readBoxes(view, start, end, callback) {
        let p = start;
        while (p + 8 <= end) {
            let size = view.getUint32(p);
            const type = String.fromCharCode(view.getUint8(p + 4), view.getUint8(p + 5), view.getUint8(p + 6), view.getUint8(p + 7));
            let header = 8;

            if (size === 1) {
                if (p + 16 > end) {
                    return p;
                }
                size = view.getUint32(p + 8) * 4294967296 + view.getUint32(p + 12);
                header = 16;
            } else if (size === 0) {
                size = end - p;
            }

            if (size < header || p + size > end) {
                return p;   // incomplete
            }

            callback(type, p + header, p + size, p);
            p += size;
        }
        return p;
    }

    function findBox(view, start, end, name) {
        let found = null;
        readBoxes(view, start, end, (type, s, e) => {
            if (found === null && type === name) {
                found = [s, e];
            }
        });
        return found;
    }

    // Parses the init segments (timescales) and returns the time span of the media segments.
    class Mp4Times {
        constructor() {
            this.tracks = new Map();   // track id -> { timescale, defaultDuration }
            this.pending = null;
        }

        parse(bytes) {
            let data = bytes;
            if (this.pending) {
                data = new Uint8Array(this.pending.length + bytes.length);
                data.set(this.pending, 0);
                data.set(bytes, this.pending.length);
                this.pending = null;
            }

            const view = new DataView(data.buffer, data.byteOffset, data.byteLength);
            let span = null;

            const consumed = readBoxes(view, 0, data.length, (type, s, e) => {
                if (type === 'moov') {
                    this.parseInit(view, s, e);
                } else if (type === 'moof') {
                    const t = this.parseFragment(view, s, e);
                    if (t) {
                        span = span ? [Math.min(span[0], t[0]), Math.max(span[1], t[1]), span[2]] : t;
                    }
                }
            });

            // only the headers are needed, keep at most an incomplete moof
            if (consumed < data.length) {
                const rest = data.subarray(consumed);
                if (rest.length >= 8 && String.fromCharCode(rest[4], rest[5], rest[6], rest[7]) === 'moof' && rest.length < 1024 * 1024) {
                    this.pending = rest.slice();
                }
            }

            return span;
        }

        parseInit(view, start, end) {
            readBoxes(view, start, end, (type, s, e) => {
                if (type === 'trak') {
                    let id = 0, timescale = 0;
                    const tkhd = findBox(view, s, e, 'tkhd');
                    if (tkhd) {
                        id = view.getUint32(tkhd[0] + (view.getUint8(tkhd[0]) === 1 ? 20 : 12));
                    }
                    const mdia = findBox(view, s, e, 'mdia');
                    const mdhd = mdia ? findBox(view, mdia[0], mdia[1], 'mdhd') : null;
                    if (mdhd) {
                        timescale = view.getUint32(mdhd[0] + (view.getUint8(mdhd[0]) === 1 ? 20 : 12));
                    }
                    const old = this.tracks.get(id) || {};
                    this.tracks.set(id, { timescale: timescale || 90000, defaultDuration: old.defaultDuration || 0 });
                } else if (type === 'mvex') {
                    readBoxes(view, s, e, (t, ts) => {
                        if (t === 'trex') {
                            const track = this.tracks.get(view.getUint32(ts + 4));
                            if (track) {
                                track.defaultDuration = view.getUint32(ts + 12);
                            }
                        }
                    });
                }
            });
        }

        // [start, end, first presentation time] in seconds
        parseFragment(view, start, end) {
            let span = null;

            readBoxes(view, start, end, (type, s, e) => {
                if (type !== 'traf') {
                    return;
                }

                const tfhd = findBox(view, s, e, 'tfhd');
                if (!tfhd) {
                    return;
                }

                const flags = view.getUint32(tfhd[0]) & 0xFFFFFF;
                let track = this.tracks.get(view.getUint32(tfhd[0] + 4));
                if (!track && this.tracks.size === 1) {
                    track = this.tracks.values().next().value;
                }
                if (!track) {
                    return;
                }

                let defaultDuration = track.defaultDuration;
                let q = tfhd[0] + 8;
                if (flags & 0x01) q += 8;
                if (flags & 0x02) q += 4;
                if (flags & 0x08) defaultDuration = view.getUint32(q);

                const tfdt = findBox(view, s, e, 'tfdt');
                if (!tfdt) {
                    return;
                }
                const decodeTime = view.getUint8(tfdt[0]) === 1
                    ? view.getUint32(tfdt[0] + 4) * 4294967296 + view.getUint32(tfdt[0] + 8)
                    : view.getUint32(tfdt[0] + 4);

                let duration = 0;
                let firstCto = null;
                readBoxes(view, s, e, (t, ts, te) => {
                    if (t !== 'trun') {
                        return;
                    }
                    const version = view.getUint8(ts);
                    const f = view.getUint32(ts) & 0xFFFFFF;
                    const count = view.getUint32(ts + 4);
                    let p = ts + 8;
                    if (f & 0x01) p += 4;
                    if (f & 0x04) p += 4;
                    for (let i = 0; i < count && p <= te; ++i) {
                        let d = defaultDuration;
                        if (f & 0x100) { d = view.getUint32(p); p += 4; }
                        if (f & 0x200) p += 4;
                        if (f & 0x400) p += 4;
                        if (f & 0x800) {
                            if (firstCto === null) {
                                firstCto = version === 0 ? view.getUint32(p) : view.getInt32(p);
                            }
                            p += 4;
                        }
                        duration += d;
                    }
                });

                const ts = track.timescale;
                const t = [decodeTime / ts, (decodeTime + duration) / ts, (decodeTime + (firstCto || 0)) / ts];
                span = span ? [Math.min(span[0], t[0]), Math.max(span[1], t[1]), Math.min(span[2], t[2])] : t;
            });

            return span;
        }
    }

    // --- events ---------------------------------------------------------------------------

    function fire(target, type) {
        const event = new Event(type);
        target.dispatchEvent(event);
        const handler = target['on' + type];
        if (typeof handler === 'function') {
            try {
                handler.call(target, event);
            } catch (e) {
                console.error(e);
            }
        }
    }

    function fireAsync(target, type) {
        setTimeout(() => fire(target, type), 0);
    }

    function fireMedia(video, type) {
        const event = new Event(type);
        event._vdrSynthetic = true;
        video.dispatchEvent(event);
    }

    // --- SourceBuffer -----------------------------------------------------------------------

    class VdrSourceBuffer extends EventTarget {
        constructor(mediaSource, type, index) {
            super();
            this._ms = mediaSource;
            this._type = type;
            this._index = index;
            this._ranges = [];
            this._times = new Mp4Times();
            this._updating = false;
            this._abortCount = 0;
            this._removed = false;
            this._video = /^\s*video\//i.test(type);

            this.mode = 'segments';
            this.timestampOffset = 0;
            this.appendWindowStart = 0;
            this.appendWindowEnd = Infinity;
            this.audioTracks = [];
            this.videoTracks = [];
            this.textTracks = [];

            this.onupdatestart = null;
            this.onupdate = null;
            this.onupdateend = null;
            this.onerror = null;
            this.onabort = null;
        }

        get updating() {
            return this._updating;
        }

        get buffered() {
            if (this._removed) {
                throw new DOMException('SourceBuffer has been removed', 'InvalidStateError');
            }
            return new VdrTimeRanges(this._ranges);
        }

        appendBuffer(data) {
            if (this._removed || this._updating) {
                throw new DOMException('SourceBuffer is not ready', 'InvalidStateError');
            }

            let bytes;
            if (data instanceof ArrayBuffer) {
                bytes = new Uint8Array(data.slice(0));
            } else if (ArrayBuffer.isView(data)) {
                bytes = new Uint8Array(data.buffer.slice(data.byteOffset, data.byteOffset + data.byteLength));
            } else {
                throw new TypeError('appendBuffer needs an ArrayBuffer or ArrayBufferView');
            }

            const ms = this._ms;
            if (ms.readyState === 'ended') {
                ms._readyState = 'open';
                fireAsync(ms, 'sourceopen');
            }

            this._updating = true;
            fireAsync(this, 'updatestart');

            if (!ms._start()) {
                this._finish(this._abortCount, false);
                return;
            }

            const span = this._times.parse(bytes);
            const abortCount = this._abortCount;
            const generation = ms._generation;

            ms._send('append/' + this._index, bytes, generation).then((ok) => {
                if (ok && span && generation === ms._generation) {
                    const offset = this.timestampOffset || 0;
                    this._ranges = addRange(this._ranges, span[0] + offset, span[1] + offset);
                    ms._segmentAppended(this, span[2] + offset);
                }
                this._finish(abortCount, ok);
            });
        }

        _finish(abortCount, ok) {
            // abort() has finished this append already
            if (abortCount !== this._abortCount) {
                return;
            }

            setTimeout(() => {
                if (abortCount !== this._abortCount) {
                    return;
                }
                this._updating = false;
                fire(this, ok ? 'update' : 'error');
                fire(this, 'updateend');
            }, 0);
        }

        remove(start, end) {
            if (this._removed || this._updating) {
                throw new DOMException('SourceBuffer is not ready', 'InvalidStateError');
            }

            // the data has been sent already, only the ranges change
            this._updating = true;
            fireAsync(this, 'updatestart');
            this._ranges = removeRange(this._ranges, start, end);

            const abortCount = this._abortCount;
            setTimeout(() => {
                if (abortCount !== this._abortCount) {
                    return;
                }
                this._updating = false;
                fire(this, 'update');
                fire(this, 'updateend');
            }, 0);
        }

        abort() {
            if (this._removed) {
                throw new DOMException('SourceBuffer has been removed', 'InvalidStateError');
            }

            this._times.pending = null;
            if (this._updating) {
                this._abortCount++;
                this._updating = false;
                fireAsync(this, 'abort');
                fireAsync(this, 'updateend');
            }
        }

        changeType(type) {
            if (!isSupportedType(type)) {
                throw new DOMException('Unsupported type ' + type, 'NotSupportedError');
            }
            this._type = type;
        }
    }

    class VdrSourceBufferList extends EventTarget {
        constructor() {
            super();
            this._list = [];
            this.onaddsourcebuffer = null;
            this.onremovesourcebuffer = null;
        }

        get length() {
            return this._list.length;
        }

        _update(list) {
            for (let i = 0; i < Math.max(list.length, this._list.length); ++i) {
                if (i < list.length) {
                    this[i] = list[i];
                } else {
                    delete this[i];
                }
            }
            this._list = list.slice();
        }

        [Symbol.iterator]() {
            return this._list[Symbol.iterator]();
        }
    }

    // --- MediaSource ------------------------------------------------------------------------

    class VdrMediaSource extends EventTarget {
        constructor() {
            super();
            this._readyState = 'closed';
            this._duration = NaN;
            this._buffers = [];
            this._sourceBuffers = new VdrSourceBufferList();
            this._video = null;
            this._started = false;
            this._generation = 0;
            this._queue = Promise.resolve();
            this._liveRange = null;
            this._time = 0;            // position until VDR reports one
            this._needBase = true;     // the first segment after start/seek gives the position
            this._vdrPaused = false;

            this.onsourceopen = null;
            this.onsourceended = null;
            this.onsourceclose = null;
        }

        static isTypeSupported(type) {
            return isSupportedType(type);
        }

        get readyState() {
            return this._readyState;
        }

        get sourceBuffers() {
            return this._sourceBuffers;
        }

        get activeSourceBuffers() {
            return this._sourceBuffers;
        }

        get duration() {
            return this._readyState === 'closed' ? NaN : this._duration;
        }

        set duration(value) {
            if (this._readyState !== 'open') {
                throw new DOMException('MediaSource is not open', 'InvalidStateError');
            }
            value = Number(value);
            if (isNaN(value) || value < 0) {
                throw new TypeError('Invalid duration');
            }
            if (value !== this._duration) {
                this._duration = value;
                if (this._video) {
                    fireMedia(this._video, 'durationchange');
                }
            }
        }

        addSourceBuffer(type) {
            if (!isSupportedType(type)) {
                throw new DOMException('Unsupported type ' + type, 'NotSupportedError');
            }
            if (this._readyState !== 'open') {
                throw new DOMException('MediaSource is not open', 'InvalidStateError');
            }
            if (this._started) {
                // the TS of VDR has been started with the existing buffers
                throw new DOMException('No more SourceBuffers', 'QuotaExceededError');
            }

            const sb = new VdrSourceBuffer(this, type, this._buffers.length);
            this._buffers.push(sb);
            this._sourceBuffers._update(this._buffers);
            fireAsync(this._sourceBuffers, 'addsourcebuffer');
            log('addSourceBuffer ' + type);
            return sb;
        }

        removeSourceBuffer(sb) {
            const i = this._buffers.indexOf(sb);
            if (i < 0) {
                throw new DOMException('Unknown SourceBuffer', 'NotFoundError');
            }
            sb.abort();
            sb._removed = true;
            this._buffers.splice(i, 1);
            this._sourceBuffers._update(this._buffers);
            fireAsync(this._sourceBuffers, 'removesourcebuffer');
        }

        endOfStream(error) {
            if (this._readyState !== 'open') {
                throw new DOMException('MediaSource is not open', 'InvalidStateError');
            }
            if (this._buffers.some((sb) => sb._updating)) {
                throw new DOMException('A SourceBuffer is updating', 'InvalidStateError');
            }

            this._readyState = 'ended';
            fireAsync(this, 'sourceended');

            if (error) {
                log('endOfStream with error ' + error);
                return;
            }

            const end = this._bufferedEnd();
            if (end > 0 && (isNaN(this._duration) || end < this._duration)) {
                this._duration = end;
            }

            if (this._started) {
                this._send('end', null, this._generation);
            }
        }

        setLiveSeekableRange(start, end) {
            this._liveRange = [start, end];
        }

        clearLiveSeekableRange() {
            this._liveRange = null;
        }

        // --- internal ---

        _bufferedEnd() {
            let end = 0;
            for (const sb of this._buffers) {
                if (sb._ranges.length) {
                    end = Math.max(end, sb._ranges[sb._ranges.length - 1][1]);
                }
            }
            return end;
        }

        _buffered() {
            let ranges = null;
            for (const sb of this._buffers) {
                ranges = ranges === null ? sb._ranges : intersectRanges(ranges, sb._ranges);
            }
            return ranges || [];
        }

        _positionBuffer() {
            return this._buffers.find((sb) => sb._video) || this._buffers[0];
        }

        // all requests in the order of the calls
        _send(path, bytes, generation) {
            const run = async () => {
                // the data before a seek is not needed anymore
                if (generation !== this._generation) {
                    return true;
                }

                while (true) {
                    try {
                        const response = await fetch(location.origin + MSE_PATH + path, {
                            method: 'POST',
                            body: bytes,
                            headers: { 'Content-Type': 'application/octet-stream' },
                            cache: 'no-store',
                            credentials: 'omit',
                        });

                        if (response.status !== 507) {
                            return response.ok;
                        }
                    } catch (e) {
                        log('request ' + path + ' failed: ' + e);
                        return false;
                    }

                    // the buffer of the transcoder is full, the player waits
                    await new Promise((resolve) => setTimeout(resolve, RETRY_DELAY));
                    if (generation !== this._generation || this._readyState === 'closed') {
                        return true;
                    }
                }
            };

            const result = this._queue.then(run);
            this._queue = result.catch(() => false);
            return result;
        }

        // starts the stream in VDR with the first segment
        _start() {
            if (this._started) {
                return true;
            }

            const video = this._video;
            if (!video || typeof window.cefMseStart !== 'function') {
                return false;
            }

            const types = this._buffers.map((sb) => sb._type);
            const duration = isFinite(this._duration) && this._duration > 0 ? String(this._duration) : 'N/A';

            log('start ' + JSON.stringify(types) + ', duration ' + duration);
            const url = window.cefMseStart(types, duration);
            if (!url) {
                log('start failed');
                return false;
            }

            this._started = true;
            this._needBase = true;

            video._vdrStatus = new VdrStatus();
            video._vdrEnded = false;
            this._statusFunction = (position, state) => html5VdrStatus(video, position, state);
            window._vdrActiveMedia = this._statusFunction;

            // the transparent video
            nativeSrc.set.call(video, url);
            window.start_video_quirk && window.start_video_quirk();
            window.promoteVideoSize && window.promoteVideoSize(video);

            // VDR plays as soon as it gets data, a paused element waits for play()
            if (video.paused && !video.autoplay) {
                window.cefPauseVideo();
                this._vdrPaused = true;
            }

            return true;
        }

        // the start of the stream in VDR after a start or seek
        _segmentAppended(sb, presentationTime) {
            if (!this._needBase || sb !== this._positionBuffer()) {
                return;
            }

            this._needBase = false;

            // VDR counts from the first picture of the stream. A reset after a seek
            // has been requested already, only the position changes.
            const status = this._video && this._video._vdrStatus;
            if (status) {
                status.base = presentationTime * 1000;
            }
        }

        _seek(time) {
            const video = this._video;
            this._time = time;

            if (!this._started) {
                return;
            }

            // VDR has not shown anything yet: the start position of the player is
            // in the sent data or its first segment follows
            const status = video._vdrStatus;
            const sb = this._positionBuffer();
            if (status && status.elapsed < 0 && sb &&
                (sb._ranges.length === 0 || sb._ranges.some((r) => time >= r[0] - RANGE_GAP && time < r[1]))) {
                return;
            }

            log('seek to ' + time);

            this._generation++;
            this._needBase = true;
            for (const b of this._buffers) {
                b._ranges = [];
                b._times.pending = null;
            }

            video._vdrEnded = false;
            if (status) {
                status.restart(time * 1000);
            }

            this._send('reset', null, this._generation);
        }

        _close() {
            if (this._readyState === 'closed') {
                return;
            }

            this._readyState = 'closed';
            this._generation++;

            // a detached MediaSource can't be attached again, the URL is not needed anymore
            for (const [url, ms] of objectUrls) {
                if (ms === this) {
                    objectUrls.delete(url);
                }
            }

            for (const sb of this._buffers) {
                sb._removed = true;
            }
            this._buffers = [];
            this._sourceBuffers._update([]);
            fireAsync(this, 'sourceclose');
        }
    }

    VdrMediaSource.canConstructInDedicatedWorker = false;

    // --- media element ----------------------------------------------------------------------

    const objectUrls = new Map();
    let urlCounter = 0;

    const nativeSrc = Object.getOwnPropertyDescriptor(HTMLMediaElement.prototype, 'src');
    const nativeSrcObject = Object.getOwnPropertyDescriptor(HTMLMediaElement.prototype, 'srcObject');
    const nativeCurrentTime = Object.getOwnPropertyDescriptor(HTMLMediaElement.prototype, 'currentTime');
    const nativeDuration = Object.getOwnPropertyDescriptor(HTMLMediaElement.prototype, 'duration');
    const nativeBuffered = Object.getOwnPropertyDescriptor(HTMLMediaElement.prototype, 'buffered');
    const nativeSeekable = Object.getOwnPropertyDescriptor(HTMLMediaElement.prototype, 'seekable');
    const nativeSetAttribute = Element.prototype.setAttribute;
    const nativeRemoveAttribute = Element.prototype.removeAttribute;
    const nativeCreateObjectURL = URL.createObjectURL;
    const nativeRevokeObjectURL = URL.revokeObjectURL;

    function mediaSourceOf(video) {
        return video._vdrMse && video._vdrMse._video === video ? video._vdrMse : null;
    }

    function installElement(video) {
        if (video._vdrMseInstalled) {
            return;
        }
        video._vdrMseInstalled = true;

        Object.defineProperty(video, 'currentTime', {
            configurable: true,
            get() {
                const ms = mediaSourceOf(this);
                if (!ms) {
                    return nativeCurrentTime.get.call(this);
                }
                const position = this._vdrStatus ? this._vdrStatus.position() : -1;
                return position >= 0 ? position / 1000 : ms._time;
            },
            set(value) {
                const ms = mediaSourceOf(this);
                if (!ms) {
                    nativeCurrentTime.set.call(this, value);
                    return;
                }

                value = Number(value);
                ms._seek(value);

                // the transparent video seeks too (seeking, seeked), a live stream gets the events only
                const duration = nativeDuration.get.call(this);
                if (ms._started && isFinite(ms._duration) && isFinite(duration) && duration > 0) {
                    nativeCurrentTime.set.call(this, Math.min(Math.max(value, 0), Math.max(duration - 0.5, 0)));
                } else {
                    setTimeout(() => {
                        fireMedia(this, 'seeking');
                        fireMedia(this, 'timeupdate');
                        fireMedia(this, 'seeked');
                    }, 0);
                }
            }
        });

        Object.defineProperty(video, 'duration', {
            configurable: true,
            get() {
                const ms = mediaSourceOf(this);
                return ms ? ms.duration : nativeDuration.get.call(this);
            }
        });

        Object.defineProperty(video, 'buffered', {
            configurable: true,
            get() {
                const ms = mediaSourceOf(this);
                return ms ? new VdrTimeRanges(ms._buffered()) : nativeBuffered.get.call(this);
            }
        });

        Object.defineProperty(video, 'seekable', {
            configurable: true,
            get() {
                const ms = mediaSourceOf(this);
                if (!ms) {
                    return nativeSeekable.get.call(this);
                }
                if (isFinite(ms._duration)) {
                    return new VdrTimeRanges([[0, ms._duration]]);
                }
                if (ms._liveRange) {
                    return new VdrTimeRanges([ms._liveRange]);
                }
                const b = ms._buffered();
                return new VdrTimeRanges(b.length ? [[b[0][0], b[b.length - 1][1]]] : []);
            }
        });

        // play and pause of the application
        video.addEventListener('playing', (e) => {
            const ms = mediaSourceOf(video);
            if (!ms || !ms._started || e._vdrSynthetic) {
                return;
            }
            if (ms._vdrPaused) {
                ms._vdrPaused = false;
                window.cefResumeVideo(String(video.currentTime));
            }
        });

        video.addEventListener('pause', (e) => {
            const ms = mediaSourceOf(video);
            if (!ms || !ms._started || e._vdrSynthetic) {
                return;
            }

            // the element has been removed from the page, the video ends
            if (!video.isConnected) {
                detach(video);
                return;
            }

            if (ms._vdrPaused || video._vdrEnded) {
                return;
            }
            ms._vdrPaused = true;
            window.cefPauseVideo();
        });

        // position and size of the video
        if (typeof ResizeObserver === 'function') {
            new ResizeObserver(() => {
                const ms = mediaSourceOf(video);
                if (ms && ms._started && window.promoteVideoSize) {
                    window.promoteVideoSize(video);
                }
            }).observe(video);
        }
    }

    function attach(video, ms, url) {
        detach(video);

        if (ms._readyState !== 'closed') {
            log('MediaSource is already attached');
            return;
        }

        installElement(video);
        video._vdrMse = ms;
        video._vdrMseUrl = url;
        ms._video = video;
        ms._readyState = 'open';

        log('attach ' + url);
        fireAsync(ms, 'sourceopen');
        fireMedia(video, 'loadstart');
    }

    function detach(video) {
        const ms = mediaSourceOf(video);
        if (!ms) {
            return;
        }

        log('detach');
        const started = ms._started;
        video._vdrMse = null;
        video._vdrMseUrl = null;
        ms._close();

        if (started) {
            if (window._vdrActiveMedia === ms._statusFunction) {
                window._vdrActiveMedia = null;
            }
            window.stop_video_quirk && window.stop_video_quirk();
            window.cefStopVideo();
        }
    }

    Object.defineProperty(HTMLMediaElement.prototype, 'src', {
        configurable: true,
        enumerable: true,
        get() {
            return this._vdrMseUrl || nativeSrc.get.call(this);
        },
        set(value) {
            const ms = objectUrls.get(String(value));
            if (ms) {
                attach(this, ms, String(value));
                return;
            }
            detach(this);
            nativeSrc.set.call(this, value);
        }
    });

    if (nativeSrcObject) {
        Object.defineProperty(HTMLMediaElement.prototype, 'srcObject', {
            configurable: true,
            enumerable: true,
            get() {
                return mediaSourceOf(this) || nativeSrcObject.get.call(this);
            },
            set(value) {
                if (value instanceof VdrMediaSource) {
                    attach(this, value, '');
                    return;
                }
                detach(this);
                nativeSrcObject.set.call(this, value);
            }
        });
    }

    Element.prototype.setAttribute = function (name, value) {
        if (this instanceof HTMLMediaElement && String(name).toLowerCase() === 'src') {
            const ms = objectUrls.get(String(value));
            if (ms) {
                attach(this, ms, String(value));
                return;
            }
            detach(this);
        }
        return nativeSetAttribute.apply(this, arguments);
    };

    Element.prototype.removeAttribute = function (name) {
        if (this instanceof HTMLMediaElement && String(name).toLowerCase() === 'src') {
            detach(this);
        }
        return nativeRemoveAttribute.apply(this, arguments);
    };

    URL.createObjectURL = function (object) {
        if (object instanceof VdrMediaSource) {
            const url = 'blob:' + location.origin + '/vdr-mediasource-' + (++urlCounter);
            objectUrls.set(url, object);
            return url;
        }
        return nativeCreateObjectURL.apply(this, arguments);
    };

    URL.revokeObjectURL = function (url) {
        if (objectUrls.has(url)) {
            objectUrls.delete(url);
            return;
        }
        return nativeRevokeObjectURL.apply(this, arguments);
    };

    // the players check the codecs with the Media Capabilities API too
    if (navigator.mediaCapabilities && typeof navigator.mediaCapabilities.decodingInfo === 'function') {
        const nativeDecodingInfo = navigator.mediaCapabilities.decodingInfo.bind(navigator.mediaCapabilities);
        navigator.mediaCapabilities.decodingInfo = function (config) {
            if (config && config.type === 'media-source' && !config.keySystemConfiguration) {
                const types = [config.video, config.audio].filter((c) => c).map((c) => c.contentType);
                const supported = types.length > 0 && types.every(isSupportedType);
                return Promise.resolve({ supported, smooth: supported, powerEfficient: supported, configuration: config });
            }
            return nativeDecodingInfo(config);
        };
    }

    window.MediaSource = VdrMediaSource;
    window.WebKitMediaSource = VdrMediaSource;
    window.SourceBuffer = VdrSourceBuffer;
    window.WebKitSourceBuffer = VdrSourceBuffer;
    window.SourceBufferList = VdrSourceBufferList;

    window._vdrMse = { VdrMediaSource, isSupportedType };
})();
