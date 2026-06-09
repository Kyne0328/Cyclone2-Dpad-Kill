//
// Cyclone 2 D-pad neutralizer for GameSir Connect (Electron / node-hid).
//
// This block is appended to node-hid's main module inside the app's asar. It
// wraps the HID class so every report the app reads -- via the 'data' event,
// read()/readSync()/readTimeout(), or getFeatureReport() -- has the D-pad forced
// to neutral before the app sees it. All other inputs pass through untouched.
//
// Patching node-hid (one chokepoint) instead of the UI code means we do not have
// to find every place GameSir Connect consumes input.
//
(function () {
    // Flip to true to log raw reports (first LOG_MAX of them) to
    // <tmp>/gamesir_hid_log.txt so the real D-pad offset can be confirmed.
    var LOG = false;
    var LOG_MAX = 60;
    var logged = 0;
    var logFile = null;

    try {
        if (LOG) {
            var os = require('os');
            var path = require('path');
            logFile = path.join(os.tmpdir(), 'gamesir_hid_log.txt');
        }
    } catch (e) { /* ignore */ }

    function logReport(data) {
        if (!LOG || logged >= LOG_MAX || !data) { return; }
        try {
            var fs = require('fs');
            var hex = Buffer.from(data).slice(0, 64).toString('hex').replace(/(..)/g, '$1 ').trim();
            fs.appendFileSync(logFile, 'len=' + data.length + ' ' + hex + '\n');
            logged++;
        } catch (e) { /* ignore */ }
    }

    //
    // Force the D-pad to neutral in a single HID report (Array or Buffer).
    //
    function scrub(data) {
        if (!data || !data.length) { return data; }

        logReport(data);

        // Vendor report (report id 0x12, 64 bytes). The D-pad hat sits in the
        // LOW nibble of byte[5] and byte[58]; 0x0F = neutral, directions = 0..7.
        // byte[58]'s HIGH nibble carries the face buttons (A/B/X/Y), so we OR in
        // 0x0F to force only the hat to neutral and leave everything else alone
        // (overwriting the whole byte would wipe ABXY).
        if (data.length >= 59 && data[0] === 0x12) {
            data[5] = data[5] | 0x0f;
            data[58] = data[58] | 0x0f;
            return data;
        }

        // Fallback: HID game-controller report, D-pad hat bits in byte[11]
        // (mask 0x1C). Used if node-hid opens the IG_01 collection instead.
        if (data.length > 11) {
            data[11] = data[11] & ~0x1c;
        }

        return data;
    }

    try {
        var HID = module.exports && module.exports.HID;
        if (!HID || !HID.prototype) { return; }

        var EventEmitter = require('events').EventEmitter;
        var origEmit = HID.prototype.emit || EventEmitter.prototype.emit;
        HID.prototype.emit = function (type) {
            if (type === 'data' && arguments[1]) { scrub(arguments[1]); }
            return origEmit.apply(this, arguments);
        };

        if (HID.prototype.read) {
            var origRead = HID.prototype.read;
            HID.prototype.read = function (cb) {
                if (typeof cb === 'function') {
                    return origRead.call(this, function (err, data) {
                        if (!err) { scrub(data); }
                        cb(err, data);
                    });
                }
                return scrub(origRead.apply(this, arguments));
            };
        }

        if (HID.prototype.readSync) {
            var origReadSync = HID.prototype.readSync;
            HID.prototype.readSync = function () {
                return scrub(origReadSync.apply(this, arguments));
            };
        }

        if (HID.prototype.readTimeout) {
            var origReadTimeout = HID.prototype.readTimeout;
            HID.prototype.readTimeout = function () {
                return scrub(origReadTimeout.apply(this, arguments));
            };
        }

        if (HID.prototype.getFeatureReport) {
            var origFeat = HID.prototype.getFeatureReport;
            HID.prototype.getFeatureReport = function () {
                return scrub(origFeat.apply(this, arguments));
            };
        }
    } catch (e) { /* ignore */ }
})();
