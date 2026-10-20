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
    const ledState = (mode, d6, d7 = d6) => ({ mode, on: d6 && d7, d6, d7 });
    const state = {
        weather: { valid: false, temp: null, hum: null, updated: null, stale: false },
        temp: 26.8, hum: 52, offline: false, clears: 0, csvRequests: 0,
        led: ledState('auto', false), ledFailure: false
    };
    const stamp = '18/10/2026 05:10:00';
    const payload = () => ({
        temp: state.temp, hum: state.hum, tmin: 24.1, tmax: 28.4, hmin: 48, hmax: 60,
        time: '18/10/2026 05:12:00', hcount: 3, histT: [26.2, 26.6, 26.8], histH: [51, 52, 52],
        moonIcon: '🌓', moonName: 'Cuarto creciente', nextNewMoon: '09/11/2026', nextFullMoon: '26/10/2026',
        weather: state.weather, led: state.led
    });
    const server = http.createServer((req, res) => {
        if (req.url === '/') { res.writeHead(200, { 'Content-Type': 'text/html; charset=utf-8' }); return res.end(html); }
        if (req.url.startsWith('/led?')) {
            assert.equal(req.method, 'PUT');
            if (state.ledFailure) { res.writeHead(503); return res.end('unavailable'); }
            const query = new URL(req.url, 'http://localhost').searchParams;
            state.led = query.get('mode') === 'auto' ?
                ledState('auto', state.weather.valid && state.weather.temp >= 35, false) :
                ledState('manual', query.get('on') === '1');
            res.writeHead(200, { 'Content-Type': 'application/json' });
            return res.end(JSON.stringify(state.led));
        }
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
        assert.equal(await page.locator('#ledSwitch').getAttribute('aria-label'), 'LEDs D6/D7');

        // El switch espera al ESP; no cambia optimistamente ni ante HTTP fallido.
        assert.equal(await page.locator('#ledSwitch').getAttribute('aria-checked'), 'false');
        await page.locator('#ledSwitch').click();
        await page.waitForFunction(() => document.getElementById('ledStatus').textContent === 'Manual · ON');
        assert.deepEqual(state.led, ledState('manual', true));
        await page.locator('#ledSwitch').click();
        await page.waitForFunction(() => document.getElementById('ledStatus').textContent === 'Manual · OFF');
        state.ledFailure = true;
        await page.locator('#ledSwitch').click();
        await page.waitForFunction(() => document.getElementById('ledError').textContent.includes('Sin confirmación'));
        assert.equal(await page.locator('#ledSwitch').getAttribute('aria-checked'), 'false');
        assert.deepEqual(state.led, ledState('manual', false));
        state.ledFailure = false;
        await page.evaluate(() => actualizarDatos());
        await page.waitForFunction(() => !document.getElementById('ledSwitch').disabled);
        await page.locator('#ledAuto').click();
        await page.waitForFunction(() => document.getElementById('ledStatus').textContent === 'Automático');
        // En automatico cada salida es independiente; no afirmar que ambas estan ON.
        state.led = ledState('auto', true, false);
        await page.evaluate(() => actualizarDatos());
        await page.waitForFunction(() => document.getElementById('ledSwitch').title === 'D6 ON · D7 OFF');
        assert.equal(await page.locator('#ledSwitch').getAttribute('aria-checked'), 'false');
        state.led = ledState('auto', false, true);
        await page.evaluate(() => actualizarDatos());
        await page.waitForFunction(() => document.getElementById('ledSwitch').title === 'D6 OFF · D7 ON');
        // Segundo navegador real: envia la orden y el primero recibe /data.
        const peer = await context.newPage();
        await peer.goto(`http://127.0.0.1:${server.address().port}`, { waitUntil: 'load' });
        await peer.waitForFunction(() => !document.getElementById('ledSwitch').disabled);
        await peer.locator('#ledSwitch').click();
        await peer.waitForFunction(() => document.getElementById('ledStatus').textContent === 'Manual · ON');
        await page.evaluate(() => actualizarDatos());
        await page.waitForFunction(() => document.getElementById('ledStatus').textContent === 'Manual · ON');
        assert.equal(await peer.locator('#ledSwitch').getAttribute('aria-checked'), 'true');
        await peer.close();

        // Respuesta demorada: conserva ON hasta recibir confirmacion de OFF.
        let release;
        let started;
        const requested = new Promise(resolve => { started = resolve; });
        const held = new Promise(resolve => { release = resolve; });
        await page.route('**/led?*', async route => {
            started(); await held;
            state.led = ledState('manual', false);
            await route.fulfill({ json: state.led });
        });
        await page.locator('#ledSwitch').click();
        await requested;
        assert.equal(await page.locator('#ledSwitch').getAttribute('aria-checked'), 'true');
        assert.ok(await page.locator('#ledSwitch').isDisabled());
        await page.evaluate(() => actualizarDatos());
        release();
        await page.waitForFunction(() => document.getElementById('ledStatus').textContent === 'Manual · OFF');
        await page.unroute('**/led?*');
        await page.route('**/led?*', route => route.abort());
        await page.locator('#ledSwitch').click();
        await page.waitForFunction(() => document.getElementById('ledError').textContent.includes('Sin confirmación'));
        assert.equal(await page.locator('#ledSwitch').getAttribute('aria-checked'), 'false');
        await page.unroute('**/led?*');
        await page.evaluate(() => actualizarDatos());
        await page.waitForFunction(() => !document.getElementById('ledSwitch').disabled);

        // Una respuesta /data anterior al comando no puede revertir su confirmacion.
        let releaseData;
        let dataStarted;
        const dataRequested = new Promise(resolve => { dataStarted = resolve; });
        const dataHeld = new Promise(resolve => { releaseData = resolve; });
        const oldPayload = JSON.stringify(payload());
        await page.route('**/data', async route => {
            dataStarted(); await dataHeld;
            await route.fulfill({ contentType: 'application/json', body: oldPayload });
        }, { times: 1 });
        await page.evaluate(() => actualizarDatos());
        await dataRequested;
        await page.locator('#ledSwitch').click();
        await page.waitForFunction(() => document.getElementById('ledStatus').textContent === 'Manual · ON');
        const oldResponse = page.waitForResponse(response => response.url().endsWith('/data'));
        releaseData();
        await oldResponse;
        await page.waitForTimeout(100);
        assert.equal(await page.locator('#ledSwitch').getAttribute('aria-checked'), 'true');

        // HTTP 200 con JSON invalido tampoco confirma el cambio.
        await page.route('**/led?*', route => route.fulfill({ json: { mode: 'manual' } }));
        await page.locator('#ledSwitch').click();
        await page.waitForFunction(() => document.getElementById('ledError').textContent.includes('Sin confirmación'));
        assert.equal(await page.locator('#ledSwitch').getAttribute('aria-checked'), 'true');
        await page.unroute('**/led?*');
        await page.evaluate(() => actualizarDatos());
        await page.waitForFunction(() => !document.getElementById('ledSwitch').disabled);

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

        // La ventana inicial distingue 0,1 y 0,5 C sin exagerar la curva.
        const escalaInicial = await page.evaluate(() => ({
            min: tempChart.options.scales.y.min,
            max: tempChart.options.scales.y.max,
            step: tempChart.options.scales.y.ticks.stepSize,
            tension: tempChart.data.datasets[0].tension,
            humTension: humChart.data.datasets[0].tension,
            ticks: tempChart.scales.y.ticks.map(t => t.value),
            pixelDelta01: Math.abs(tempChart.scales.y.getPixelForValue(26.8) -
                tempChart.scales.y.getPixelForValue(26.7)),
            pixelDelta05: Math.abs(tempChart.scales.y.getPixelForValue(26.8) -
                tempChart.scales.y.getPixelForValue(26.3)),
            chartHeight: tempChart.chartArea.bottom - tempChart.chartArea.top
        }));
        assert.equal(escalaInicial.max - escalaInicial.min, 3);
        assert.equal(escalaInicial.step, 0.5);
        assert.equal(escalaInicial.tension, 0);
        assert.equal(escalaInicial.humTension, 0.4);
        assert.ok(escalaInicial.ticks.slice(1).every((tick, i) =>
            Math.abs(tick - escalaInicial.ticks[i] - 0.5) < 0.001));
        assert.ok(escalaInicial.pixelDelta01 > escalaInicial.chartHeight * 0.025);
        assert.ok(escalaInicial.pixelDelta01 < escalaInicial.chartHeight * 0.045);
        assert.ok(escalaInicial.pixelDelta05 > escalaInicial.chartHeight * 0.15);
        assert.ok(escalaInicial.pixelDelta05 < escalaInicial.chartHeight * 0.20);

        for (const temp of [26.7, 26.2, 26.7]) {
            state.temp = temp;
            await page.evaluate(() => actualizarDatos());
            await page.waitForFunction(value => tempChart.data.datasets[0].data.at(-1) === value, temp);
            const escala = await page.evaluate(() => ({
                min: tempChart.options.scales.y.min, max: tempChart.options.scales.y.max
            }));
            assert.deepEqual(escala, { min: escalaInicial.min, max: escalaInicial.max });
            assert.equal(await page.locator('#temp').textContent(), temp.toFixed(1));
        }

        state.temp = 23.8;
        await page.evaluate(() => actualizarDatos());
        await page.waitForFunction(() => tempChart.data.datasets[0].data.at(-1) === 23.8);
        const escalaAmpliada = await page.evaluate(() => ({
            min: tempChart.options.scales.y.min, max: tempChart.options.scales.y.max
        }));
        assert.ok(escalaAmpliada.min < 23.8 && escalaAmpliada.max > 26.8);
        for (const temp of [23.7, 23.8, 23.7]) {
            state.temp = temp;
            await page.evaluate(() => actualizarDatos());
            await page.waitForFunction(value => tempChart.data.datasets[0].data.at(-1) === value, temp);
            assert.deepEqual(await page.evaluate(() => ({
                min: tempChart.options.scales.y.min, max: tempChart.options.scales.y.max
            })), escalaAmpliada);
        }

        state.temp = 30.8;
        await page.evaluate(() => actualizarDatos());
        await page.waitForFunction(() => tempChart.data.datasets[0].data.at(-1) === 30.8);
        assert.ok(await page.evaluate(() => {
            const values = tempChart.data.datasets[0].data;
            const y = tempChart.options.scales.y;
            return y.max - y.min >= 3 && Math.min(...values) > y.min &&
                Math.max(...values) < y.max && values.at(-1) === 30.8;
        }));
        await page.evaluate(() => {
            tempChart.data.labels = Array(40).fill('muestra estable');
            tempChart.data.datasets[0].data = Array(40).fill(26.8);
            actualizarEscalaTemperatura();
            tempChart.update();
        });
        assert.equal(await page.evaluate(() =>
            tempChart.options.scales.y.max - tempChart.options.scales.y.min), 3);

        // Un escalon de /data debe llegar sin otro filtro a ambas curvas.
        for (const [temp, hum] of [[34.8, 80], [20.8, 35], [26.8, 52]]) {
            state.temp = temp; state.hum = hum;
            await page.evaluate(() => actualizarDatos());
            await page.waitForFunction(([t, h]) =>
                tempChart.data.datasets[0].data.at(-1) === t &&
                humChart.data.datasets[0].data.at(-1) === h, [temp, hum]);
            assert.equal(await page.locator('#temp').textContent(), temp.toFixed(1));
            assert.equal(await page.locator('#hum').textContent(), hum.toFixed(0));
        }
        assert.deepEqual(await page.evaluate(() => tempHourChart.data.datasets[0].data.map(p => p.y)), [26.2, 26.6, 26.8]);
        assert.deepEqual(await page.evaluate(() => humHourChart.data.datasets[0].data.map(p => p.y)), [51, 52, 52]);
        assert.match(html, /setInterval\(actualizarDatos, 2000\)/);

        const layouts = [];
        await page.mouse.move(0, 0);
        const cardBase = await page.locator('.environment-card').evaluateAll(cards => cards.map(card => {
            const style = getComputedStyle(card);
            return { transform: style.transform, transition: style.transition, shadow: style.boxShadow };
        }));
        for (const card of await page.locator('.environment-card').all()) {
            await card.hover();
            await page.waitForTimeout(50);
            const cardHover = await page.locator('.environment-card').evaluateAll(cards => cards.map(card => {
                const style = getComputedStyle(card);
                return { transform: style.transform, shadow: style.boxShadow };
            }));
            assert.deepEqual(cardHover, cardBase.map(({ transform, shadow }) => ({ transform, shadow })));
        }
        assert.ok(cardBase.every(({ transition }) => !/transform|box-shadow/.test(transition)));
        const clearBase = await page.locator('#btnClear').evaluate(button => ({
            transform: getComputedStyle(button).transform, background: getComputedStyle(button).backgroundColor
        }));
        await page.locator('#btnClear').hover();
        await page.waitForTimeout(250);
        const clearHover = await page.locator('#btnClear').evaluate(button => ({
            transform: getComputedStyle(button).transform, background: getComputedStyle(button).backgroundColor
        }));
        assert.notDeepEqual(clearHover, clearBase);
        await page.mouse.move(0, 0);
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
            const ledLayout = await page.evaluate(() => {
                const a = document.querySelector('.local-heading .card-title-group').getBoundingClientRect();
                const b = document.querySelector('.led-control').getBoundingClientRect();
                return { titleRight: a.right, controlLeft: b.left, topDelta: Math.abs(a.top - b.top) };
            });
            assert.ok(ledLayout.controlLeft >= ledLayout.titleRight, `LED solapado a ${width}px`);
            assert.ok(ledLayout.topDelta < 2, `LED fuera del encabezado a ${width}px`);
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
        console.log('PASS: LED manual/auto, confirmaciones, errores HTTP/red y sincronizacion; escalones sin EWMA, historicos y polling 2 s; estados meteorologicos, luna, 4 graficos, CSV y 5 anchos sin overflow.');
        console.log('Capturas y resultados:', output);
    } finally {
        if (browser) await browser.close();
        await new Promise(resolve => {
            server.close(resolve);
            // Las pruebas de fallo de red pueden dejar sockets del fixture abiertos.
            server.closeAllConnections();
        });
    }
})().catch(error => { console.error(error); process.exitCode = 1; });
