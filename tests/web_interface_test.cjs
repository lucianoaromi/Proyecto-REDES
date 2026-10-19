// UI real extraida del sketch; servidor y mediciones simulados, sin ESP8266 ni claves.
// PLAYWRIGHT_MODULE_PATH permite usar Playwright instalado fuera del repositorio.
const { chromium } = require(process.env.PLAYWRIGHT_MODULE_PATH || 'playwright');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const http = require('node:http');
const path = require('node:path');

(async () => {
    const source = fs.readFileSync(path.join(__dirname, '../ESP8266_DHT22/ESP8266_DHT22.ino'), 'utf8');
    const html = source.match(/R"rawliteral\(([\s\S]*?)\)rawliteral"/)[1];
    const output = process.env.WEB_TEST_OUTPUT || path.join(require('node:os').tmpdir(), 'redes-web-test');
    fs.mkdirSync(output, { recursive: true });
    const state = {
        weather: { valid: false, temp: null, hum: null, updated: null, stale: false },
        offline: false, clears: 0, csvRequests: 0
    };
    const stamp = '18/10/2026 05:10:00';
    const payload = () => ({
        temp: 26.8, hum: 52, tmin: 24.1, tmax: 28.4, hmin: 48, hmax: 60,
        time: '18/10/2026 05:12:00', hcount: 3, histT: [26.2, 26.6, 26.8], histH: [51, 52, 52],
        moonIcon: '🌓', moonName: 'Cuarto creciente', nextNewMoon: '09/11/2026', nextFullMoon: '26/10/2026',
        weather: state.weather
    });
    const server = http.createServer((req, res) => {
        if (req.url === '/') { res.writeHead(200, { 'Content-Type': 'text/html; charset=utf-8' }); return res.end(html); }
        if (req.url === '/data') {
            res.writeHead(state.offline ? 503 : 200, { 'Content-Type': 'application/json' });
            return res.end(state.offline ? 'unavailable' : JSON.stringify(payload()));
        }
        if (req.url === '/csvinfo') { res.writeHead(200, { 'Content-Type': 'application/json' }); return res.end(JSON.stringify({ first: '18/10/2026', last: '18/10/2026' })); }
        if (req.url === '/csv') {
            state.csvRequests++;
            res.writeHead(200, { 'Content-Type': 'text/csv', 'Content-Disposition': 'attachment; filename="historial_prueba.csv"' });
            return res.end('FechaHora,Temperatura,Hum.edad\r\n18/10/2026 05:10:00,26.8,52\r\n');
        }
        if (req.url === '/clearcsv') { state.clears++; return res.end('CSV borrado correctamente'); }
        res.writeHead(404); res.end();
    });
    await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
    let browser;
    try {
        browser = await chromium.launch({ channel: 'msedge', headless: true });
        const context = await browser.newContext({ viewport: { width: 1280, height: 900 }, acceptDownloads: true });
        const page = await context.newPage();
        const errors = [];
        const requests = [];
        page.on('pageerror', error => errors.push(error.message));
        page.on('request', req => requests.push(req.url()));
        page.on('dialog', dialog => dialog.accept());
        await page.goto(`http://127.0.0.1:${server.address().port}`, { waitUntil: 'load' });
        await page.waitForFunction(() => typeof Chart !== 'undefined' && document.getElementById('temp').textContent === '26.8');
        await page.evaluate(() => document.fonts.ready);
        assert.equal(await page.locator('#weatherTemp').textContent(), '--');
        assert.equal(await page.locator('#weatherUpdated').textContent(), 'Sin datos');

        state.weather = { valid: true, temp: 32.4, hum: 0, updated: stamp, stale: false };
        await page.evaluate(() => actualizarDatos());
        await page.waitForFunction(() => document.getElementById('weatherTemp').textContent === '32.4');
        assert.equal(await page.locator('#weatherHum').textContent(), '0');
        assert.equal(await page.locator('#weatherUpdated').textContent(), stamp);
        assert.notEqual(await page.locator('#time').textContent(), stamp);
        state.weather.hum = 64;
        state.weather.stale = true;
        await page.evaluate(() => actualizarDatos());
        await page.waitForFunction(() => document.getElementById('weatherStatus').textContent === 'Datos anteriores');
        assert.equal(await page.locator('#weatherTemp').textContent(), '32.4');
        assert.equal(await page.locator('#weatherUpdated').textContent(), stamp);

        state.offline = true;
        await page.evaluate(() => actualizarDatos());
        await page.waitForFunction(() => document.getElementById('weatherStatus').textContent === 'Sin conexión local');
        assert.equal(await page.locator('#weatherUpdated').textContent(), stamp);
        state.offline = false;
        state.weather.stale = false;
        await page.evaluate(() => actualizarDatos());
        await page.waitForFunction(() => document.getElementById('weatherStatus').textContent === 'Datos disponibles');

        assert.equal(await page.locator('#moonName').textContent(), 'Cuarto creciente');
        assert.equal(await page.locator('#nextNewMoon').textContent(), '09/11/2026');
        assert.equal(await page.locator('#nextFullMoon').textContent(), '26/10/2026');
        assert.equal(await page.locator('#csvFirst').textContent(), '18/10/2026');
        assert.equal(await page.locator('#csvLast').textContent(), '18/10/2026');
        assert.equal(await page.evaluate(() => Object.keys(Chart.instances).length), 4);
        assert.equal(await page.evaluate(() => tempHourChart.data.datasets[0].data.length), 3);
        assert.ok(await page.evaluate(() => tempChart.data.datasets[0].data.length > 0));

        const layouts = [];
        for (const width of [1280, 768, 680, 390, 320]) {
            await page.setViewportSize({ width, height: 900 });
            await page.waitForTimeout(300); // Permitir el resize de Chart.js y las fuentes.
            const layout = await page.evaluate(() => {
                const cards = [...document.querySelectorAll('.environment-card')].map(e => e.getBoundingClientRect().toJSON());
                return { width: innerWidth, scroll: document.documentElement.scrollWidth, cards,
                    moon: document.querySelector('.lunar-card').getBoundingClientRect().toJSON(),
                    charts: document.querySelector('.charts').getBoundingClientRect().toJSON(),
                    canvases: [...document.querySelectorAll('.chart-canvas')].map(e => ({
                        container: e.getBoundingClientRect().width,
                        canvas: e.querySelector('canvas').getBoundingClientRect().width
                    })),
                    clear: document.getElementById('btnClear').getBoundingClientRect().toJSON() };
            });
            assert.ok(layout.scroll <= width, `overflow horizontal a ${width}px`);
            if (width > 680) {
                assert.ok(Math.abs(layout.cards[0].top - layout.cards[1].top) < 1);
                assert.ok(Math.abs(layout.cards[0].height - layout.cards[1].height) < 1);
            } else assert.ok(layout.cards[1].top >= layout.cards[0].bottom);
            assert.ok(layout.moon.top >= Math.max(...layout.cards.map(c => c.bottom)));
            assert.ok(layout.charts.top >= layout.moon.bottom);
            assert.ok(layout.clear.width >= 44 && layout.clear.height >= 44);
            assert.ok(layout.canvases.every(c => c.canvas >= c.container * 0.9), `graficos pequenos a ${width}px`);
            layouts.push(layout);
            if (width === 1280) {
                await page.screenshot({ path: path.join(output, 'desktop.png'), fullPage: true });
                await page.screenshot({ path: path.join(output, 'desktop-top.png'), clip: { x: 0, y: 0, width: 1280, height: 680 } });
            }
            if (width === 390) await page.screenshot({ path: path.join(output, 'mobile.png'), fullPage: true });
        }

        const downloadPromise = page.waitForEvent('download');
        await page.locator('#btnCSV').click();
        const download = await downloadPromise;
        assert.equal(download.suggestedFilename(), 'historial_prueba.csv');
        assert.ok(state.csvRequests > 0);
        const clearRequest = page.waitForRequest(req => req.url().endsWith('/clearcsv'));
        await page.locator('#btnClear').click();
        await clearRequest;
        assert.ok(state.clears > 0); // Solo borra la fixture de este servidor local.
        assert.deepEqual(errors, []);
        assert.equal(requests.filter(url => /api\.(weatherapi|telegram)\./.test(url)).length, 0);
        fs.writeFileSync(path.join(output, 'layout-results.json'), JSON.stringify(layouts, null, 2));
        console.log('PASS: estados sin datos/validos/anteriores/offline, hora meteorologica fija, luna, 4 graficos, CSV y 5 anchos sin overflow.');
        console.log('Capturas y resultados:', output);
    } finally {
        if (browser) await browser.close();
        await new Promise(resolve => server.close(resolve));
    }
})().catch(error => { console.error(error); process.exitCode = 1; });
