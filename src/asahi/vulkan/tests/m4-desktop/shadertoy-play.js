// Chromium CDP action: wait for the embedded texture and first shader draw,
// then start the page's bounded ten-second animation. CDP must awaitPromise.
return new Promise((resolve, reject) => {
    const deadline = performance.now() + 5000;
    function start() {
        const report = JSON.parse(document.documentElement.dataset.m4Probe || 'null');
        if (report && report.error) return reject(new Error(report.error));
        if (report && report.ready) {
            document.querySelector('#play').click();
            return resolve(report);
        }
        if (performance.now() >= deadline) return reject(new Error('Buoy startup timeout'));
        setTimeout(start, 50);
    }
    start();
});
