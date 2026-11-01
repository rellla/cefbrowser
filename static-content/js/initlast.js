function prepareElements() {
    const objects = document.getElementsByTagName("object");
    for (let i = 0; i < objects.length; i++) {
        let node = objects[i];

        let mimeType = node.type;
        if (!node.type) {
            node.style.visibility = 'hidden';
            continue;
        }

        mimeType = mimeType.toLowerCase();

        if (mimeType.lastIndexOf('video/broadcast', 0) >= 0) { // TV
            console.log("Init Found TV on node: " + node);
            window.addVideoOverlay(node);
            addNodeFunctions(node);
            addBroadcastFunctions(node);

            setTimeout(() => {
                promoteVideoSize(node);
            }, 10);
        } else if (mimeType.lastIndexOf("application/oipfapplicationmanager", 0) >= 0 ||
                   mimeType.lastIndexOf("application/oipfconfiguration", 0) >= 0 ||
                   mimeType.lastIndexOf("application/oipfcapabilities", 0) >= 0 ||
                   mimeType.lastIndexOf("application/oipfdrmagent", 0) >= 0 ||
                   mimeType.lastIndexOf("application/hbbtvmediasynchroniser", 0) >= 0) {
            node.style.visibility = 'hidden';
            console.log('Hide Object ' + mimeType);
        } else {
            console.log('Unknown object ' + mimeType);
        }
    }

    const videoElement = document.getElementById('video');
    if (videoElement) {
        videoElement.bindToCurrentChannel = videoElement.bindToCurrentChannel || function() {
            return window.HBBTV_POLYFILL_NS.currentChannel;
        }

        videoElement.setFullScreen = videoElement.setFullScreen || function() {
            let bodyPos = document.getElementsByTagName('body')[0].getBoundingClientRect();

            videoElement.x = 0;
            videoElement.y = 0;
            videoElement.width = bodyPos.width;
            videoElement.height = bodyPos.height;
        }

        setTimeout(() => {
            promoteVideoSize(videoElement);
        }, 10);
    }
}

prepareElements();

document.body.style["font-family"] = "Tiresias";
document.body.style["overflow"] = "hidden";

// hack for ZDF to force HTML5 video
const _vdrZdfHtml5 = !window._vdrOptions || window._vdrOptions.zdfQuirks !== false;
setTimeout(() => {
    if (_vdrZdfHtml5 && typeof appStorage != "undefined") {
        appStorage.getItem('config').isHbbTv = false;
    }
}, 10);

// ARD channels also uses window.GLOBALS.htmlfive. But the application is not yet prepared.
if (window.GLOBALS) {
    let location = document.location.toString();
    // window.GLOBALS.htmlfive = location.includes("hbbtv.zdf.de") || location.includes("tv.ardmediathek.de");
    window.GLOBALS.htmlfive = _vdrZdfHtml5 && location.includes("hbbtv.zdf.de");

    // ZDF (prevent pending CORS related requests)
    if (location.includes("hbbtv.zdf.de")) {
        window.GLOBALS.noapicall = true;
    }
}