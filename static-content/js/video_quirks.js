function _quirk_add_class(id, clazz, tagName, classname) {
    let el;
    if (id !== null) {
        el = document.querySelectorAll('#' + id);
    } else if (clazz !== null) {
        el = document.getElementsByClassName(clazz);
    } else {
        el = document.querySelectorAll(tagName);
    }

    if (typeof el !== 'undefined' && el != null && el.length > 0) {
        for (let i = 0; i < el.length; ++i) {
            el[i].classList.add(classname);
        }
    }
}

function _quirk_remove_class(id, clazz, tagName, classname) {
    let el;

    if (id !== null) {
        el = document.querySelectorAll('#' + id);
    } else if (clazz !== null) {
        el = document.getElementsByClassName(clazz);
    } else {
        el = document.querySelectorAll(tagName);
    }

    if (typeof el !== 'undefined' && el != null && el.length > 0) {
        for (let i = 0; i < el.length; ++i) {
            el[i].classList.remove(classname);
        }
    }
}

function _quirk_hide_element(id, clazz, tagName) {
    _quirk_add_class(id, clazz, tagName, 'quirk_hide_element');}

function _quirk_unhide_element(id, clazz, tagName) {
    _quirk_remove_class(id, clazz, tagName, 'quirk_hide_element');
}

function activate_quirks(isStart) {
    console.log("document.location.href = " + document.location.href);

    // Arte.tv
    if (document.location.href.search("www.arte.tv") > 0) {
        if (isStart) {
            _quirk_hide_element('player', null, null);
        } else {
            _quirk_unhide_element('player', null, null);
        }
    } else if (document.location.href.search("hbbtv.redbutton.de/extern/redorbit/hbbtv/apps/") > 0) {
        if (isStart) {
            _quirk_hide_element('contentHub-home', null, null);
            _quirk_hide_element('stage', null, null);
            _quirk_hide_element('slide-wrapper', null, null);

            _quirk_add_class('screen', null, null, 'quirk_background_transparent');
            _quirk_add_class(null, null, 'body', 'quirk_background_transparent');
        } else {
            _quirk_unhide_element('contentHub-home', null, null);
            _quirk_unhide_element('stage', null, null);
            _quirk_unhide_element('slide-wrapper', null, null);

            _quirk_remove_class('screen', null, null, 'quirk_background_color1');
            _quirk_remove_class(null, null, 'body', 'quirk_background_color2');
        }
    } else if (document.location.href.search("-hbbtv.zdf.de") > 0) {
        if (isStart) {
            _quirk_hide_element('root', null, null);
            document.body.style.background = 'transparent';
            document.getElementsByTagName('html')[0].style.setProperty('background', 'transparent');
        } else {
            _quirk_unhide_element('root', null, null);
            document.body.style.background = '#0d1118';
            document.getElementsByTagName('html')[0].style.setProperty('background', '#0d1118');
        }
    } else if (document.location.href.search("http://127.0.0.1") >= 0 && document.location.href.search("/application/iptv/index.html") > 0) {
        if (isStart) {
            _quirk_hide_element('app_area', null, null);
        } else {
            _quirk_unhide_element('app_area', null, null);
        }
    }
}

window.start_video_quirk = function() {
    activate_quirks(true);
};

window.stop_video_quirk = function() {
    activate_quirks(false);
};

window.start_video_quirk_Fullscreen = function() {
    if (document.location.href.search(".rtl-hbbtv.de/") > 0) {
        _quirk_hide_element(null, 'videochannel', null);
    }
};

window.stop_video_quirk_Fullscreen = function() {
    if (document.location.href.search(".rtl-hbbtv.de/") > 0) {
        _quirk_unhide_element(null, 'videochannel', null);
    }
};
