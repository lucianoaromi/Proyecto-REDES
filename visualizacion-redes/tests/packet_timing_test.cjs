/* Pruebas de comportamiento: distancia visible y reloj del navegador, sin APIs del motor. */
'use strict';
const assert = require('node:assert/strict');
const path = require('node:path');
const { pathToFileURL } = require('node:url');
const { chromium } = require(process.env.PLAYWRIGHT_MODULE_PATH || 'playwright');

(async () => {
  const browser = await chromium.launch({ channel: process.env.VISUALIZATION_BROWSER || 'msedge', headless: true });
  let checks = 0, measurements = 0;
  const check = (condition, message) => { assert.ok(condition, message); checks++; };
  const rates = [];
  try {
    const context = await browser.newContext({ viewport: { width: 1440, height: 1050 }, offline: true });
    const page = await context.newPage(), errors = [], requests = [];
    page.on('pageerror', error => errors.push(error.message));
    page.on('console', message => { if (message.type() === 'error') errors.push(message.text()); });
    page.on('request', request => { if (/^https?:/.test(request.url())) requests.push(request.url()); });
    await page.goto(pathToFileURL(path.resolve(__dirname, '../index.html')).href);
    await page.clock.install({ time: new Date('2026-10-23T12:00:00Z') });
    await page.clock.pauseAt(new Date('2026-10-23T12:00:01Z'));

    const snapshot = () => page.evaluate(() => ({
      step: document.getElementById('step-counter').textContent,
      operation: document.getElementById('info-operation').textContent,
      time: performance.now(),
      packets: [...document.querySelectorAll('.packet')].flatMap((node, index) => {
        if (getComputedStyle(node).display === 'none' || !node.closest('svg').getBoundingClientRect().width) return [];
        const matrix = node.getScreenCTM();
        return [{ index, label: node.textContent, x: matrix.e, y: matrix.f, logical: node.getAttribute('transform') }];
      })
    }));

    async function select(scenario, stage, view = 'network', speed = 1, client = 'pc') {
      await page.locator(`[data-scenario="${scenario}"]`).click();
      if (await page.locator('#client').isEnabled()) await page.locator('#client').selectOption(client);
      await page.locator(`#tab-${view}`).click();
      await page.locator('#speed').fill(String(speed));
      await page.locator('#timeline button').nth(stage).click();
    }

    const cases = [
      { scenario: 'http', stage: 0, route: ['link-pc-ap', 'link-ap-esp'], labels: ['GET / →'] },
      { scenario: 'led', stage: 1, route: ['link-pc-ap', 'link-ap-esp'], labels: ['PUT /led →'] },
      { scenario: 'led', stage: 2, view: 'hardware', route: ['link-d6'], labels: ['D6 →'], simultaneous: 2 },
      { scenario: 'weather', stage: 1, route: ['-link-ap-esp'], labels: ['Hacia DNS', 'Desde DNS'] },
      { scenario: 'weather', stage: 2, route: ['-link-ap-esp', 'link-ap-net', 'link-net-weather'], labels: ['SYN →', '← SYN-ACK', 'ACK →'] },
      { scenario: 'weather', stage: 3, route: ['-link-ap-esp', 'link-ap-net', 'link-net-weather'], labels: ['TLS →', '← TLS'] },
      { scenario: 'weather', stage: 4, route: ['-link-ap-esp', 'link-ap-net', 'link-net-weather'], labels: ['HTTPS · GET →'] },
      { scenario: 'telegram', stage: 4, route: ['-link-ap-esp', 'link-ap-net', 'link-net-telegram'], labels: ['HTTPS · GET →'] },
      { scenario: 'wifi', stage: 1, route: ['-link-ap-esp'], labels: ['Asociación →', '← AP'] },
      { scenario: 'portal', stage: 3, route: ['link-pc-esp'], labels: ['GET /wifi →', '← HTML'] },
      { scenario: 'ntp', stage: 2, route: ['-link-ap-esp', 'link-ap-net', 'link-net-ntp'], labels: ['SNTP →'] },
      { scenario: 'general', stage: 15, view: 'hardware', route: ['-link-dht'], labels: ['Datos DHT →'] }
    ];

    async function geometry(route) {
      return page.evaluate(route => {
        const svg = document.getElementById(document.getElementById('view-network').hidden ? 'hardware-svg' : 'network-svg');
        const transform = svg.getScreenCTM(), scale = Math.hypot(transform.a, transform.b);
        let total = 0, previous = null, firstLength = 0;
        for (const [index, key] of route.entries()) {
          const reversed = key.startsWith('-'), item = document.getElementById(key.replace(/^-/, ''));
          const length = item.getTotalLength(), start = item.getPointAtLength(reversed ? length : 0);
          if (previous) total += Math.hypot(start.x - previous.x, start.y - previous.y);
          total += length; previous = item.getPointAtLength(reversed ? 0 : length);
          if (index === 0) firstLength = length;
        }
        return { pixels: total * scale, firstPixels: firstLength * scale, scale };
      }, route);
    }

    async function measure(speed, label) {
      const before = await snapshot();
      await page.clock.runFor(160); // diez frames: comparar distancia real en pantalla.
      const after = await snapshot();
      check(before.step === after.step && before.operation === after.operation, `${label}: ficha sincronizada durante el movimiento`);
      check(before.packets.length > 0 && after.packets.length === before.packets.length, `${label}: paquete visible y sin recambio de lote`);
      for (const start of before.packets) {
        const end = after.packets.find(p => p.index === start.index);
        check(!!end && end.label === start.label, `${label}: mismo paquete`);
        const rate = Math.hypot(end.x - start.x, end.y - start.y) / ((after.time - before.time) / 1000);
        // La cuerda de una curva es levemente menor que su longitud de arco.
        check(Math.abs(rate - 40 * speed) <= 40 * speed * .045 + .1, `${label}: velocidad ${rate.toFixed(2)}, esperada ${40 * speed} px/s`);
        rates.push(rate / speed); measurements++;
      }
    }

    for (const speed of [.5, 1, 1.5, 2]) {
      for (const item of cases) {
        await select(item.scenario, item.stage, item.view, speed);
        const { pixels } = await geometry(item.route);
        const travel = pixels / 40 * 1000;
        await page.locator('#play').click();
        // Ejecutar el primer RAF antes de saltar esperas con el reloj simulado.
        await page.clock.runFor(32);
        let advanced = 32;
        for (const [batch, label] of item.labels.entries()) {
          const target = Math.ceil((900 + batch * (travel + 650) + 320) / speed);
          await page.clock.fastForward(target - advanced); advanced = target;
          // Alinear las dos muestras con RAF regulares después del salto del reloj.
          await page.clock.runFor(32); advanced += 32;
          const visible = await snapshot();
          check(visible.packets.length === (item.simultaneous || 1), `${item.scenario}/${batch}/${speed}: lotes consecutivos no superpuestos`);
          check(visible.packets[0].label === label, `${item.scenario}/${batch}/${speed}: orden de solicitud/respuesta`);
          await measure(speed, `${item.scenario}/${batch}/${speed}`); advanced += 160;
        }
        const beforeArrival = Math.ceil((900 + item.labels.length * travel + (item.labels.length - 1) * 650 - 200) / speed);
        await page.clock.fastForward(beforeArrival - advanced); advanced = beforeArrival;
        check((await snapshot()).packets.length === (item.simultaneous || 1), `${item.scenario}/${speed}: duración proporcional a la distancia hasta la llegada`);
        const tail = Math.ceil((900 + item.labels.length * travel + (item.labels.length - 1) * 650 + 400) / speed);
        await page.clock.fastForward(tail - advanced);
        check((await snapshot()).packets.length === 0, `${item.scenario}/${speed}: llegada y separación final`);
        check(await page.locator('#step-counter').textContent() === String(item.stage + 1).padStart(2, '0'), `${item.scenario}/${speed}: no cambia el panel antes del último paquete`);
      }
      console.log(`Velocidad ${speed}x: recorridos cortos, largos, GPIO y lotes secuenciales verificados.`);
    }

    // Continuidad al atravesar el AP: el SVG original corta sus líneas en el borde del nodo.
    await select('http', 0);
    const local = await geometry(['link-pc-ap', 'link-ap-esp']);
    await page.locator('#play').click();
    await page.clock.runFor(Math.ceil(900 + local.firstPixels / 40 * 1000 - 80));
    const boundaryStart = await snapshot();
    await page.clock.runFor(160);
    const boundaryEnd = await snapshot();
    const jump = Math.hypot(boundaryEnd.packets[0].x - boundaryStart.packets[0].x, boundaryEnd.packets[0].y - boundaryStart.packets[0].y);
    check(jump > 5 && jump < 8, 'sin teletransporte entre enlaces del AP');

    // El selector cambia la pendiente, sin recalcular la posición ya recorrida.
    await select('http', 0);
    await page.locator('#play').click(); await page.clock.runFor(1220);
    const beforeSpeed = await snapshot();
    await page.locator('#speed').fill('2');
    const afterSpeed = await snapshot();
    const coordinates = packet => packet.logical.match(/-?\d+(?:\.\d+)?/g).map(Number);
    const a = coordinates(beforeSpeed.packets[0]), b = coordinates(afterSpeed.packets[0]);
    const { scale } = await geometry(['link-pc-ap', 'link-ap-esp']);
    // Al aplicar el factor se consume la fracción pendiente del último RAF (< un frame).
    check(Math.hypot(b[0] - a[0], b[1] - a[1]) * scale <= 40 * .020 + .02, 'cambiar velocidad solo completa la fracción pendiente de frame, sin saltar');
    await page.clock.runFor(32);
    await measure(2, 'cambio de velocidad en curso');
    await page.locator('#play').click();
    const paused = await snapshot();
    await page.clock.fastForward(60000);
    check(JSON.stringify((await snapshot()).packets) === JSON.stringify(paused.packets), 'pausa congela posición y recorrido');
    await page.locator('#play').click(); await page.clock.runFor(200);
    await measure(2, 'continuación');

    // Mantener píxeles/s al redimensionar y alternar pestañas, sin reiniciar el avance.
    for (const width of [320, 768, 1920]) {
      await page.setViewportSize({ width: 1440, height: 1050 });
      await select('led', 2, 'hardware');
      await page.locator('#play').click(); await page.clock.runFor(1220);
      const beforeResize = await snapshot();
      await page.setViewportSize({ width, height: 1050 });
      check((await snapshot()).packets[0].logical === beforeResize.packets[0].logical, `${width}px: conserva posición lógica`);
      await measure(1, `${width}px: escala visual`);
      await page.locator('#tab-network').click();
      await page.locator('#tab-hardware').click();
      await measure(1, `${width}px: cambio de pestaña`);
    }

    await page.setViewportSize({ width: 1440, height: 1050 });
    await select('led', 2, 'hardware', 2);
    await page.locator('#play').click();
    await page.clock.runFor(9500);
    check(await page.locator('#step-counter').textContent() === '03', 'a 2x conserva al menos diez segundos reales para lectura');
    check((await snapshot()).packets.length === 0, 'lectura posterior a la llegada, sin partículas residuales');
    await page.locator('#play').click();
    await page.clock.fastForward(60000);
    check(await page.locator('#step-counter').textContent() === '03', 'la pausa también detiene el reloj de lectura');
    await page.locator('#play').click();
    await page.clock.fastForward(120000);
    check(await page.locator('body').getAttribute('data-playback') === 'complete', 'continúa y termina tras la espera de lectura');

    for (const scenario of ['http', 'led', 'weather', 'telegram', 'wifi', 'portal', 'ntp', 'general']) {
      await page.locator(`[data-scenario="${scenario}"]`).click();
      await page.locator('#speed').fill('1');
      await page.locator('#play').click();
      await page.clock.runFor(32);
      await page.clock.fastForward(30 * 60 * 1000);
      check(await page.locator('body').getAttribute('data-playback') === 'complete', `${scenario}: reproducción automática completa`);
      check(await page.locator('.packet').count() === 0, `${scenario}: sin partículas residuales al finalizar`);
      await page.locator('#reset').click();
      check(await page.locator('#step-counter').textContent() === '00', `${scenario}: reiniciar conserva controles`);
    }
    check(errors.length === 0, `sin errores: ${errors.join('; ')}`);
    check(requests.length === 0, 'sin conexiones externas');
    console.log(JSON.stringify({ result: 'OK', checks, measurements, normalizedPixelsPerSecond: { min: Math.min(...rates), max: Math.max(...rates), expected: 40 }, speeds: [.5, 1, 1.5, 2] }, null, 2));
  } catch (error) {
    console.error(error); process.exitCode = 1;
  } finally {
    // Detener RAF y relojes simulados de la app antes de destruir las páginas.
    for (const context of browser.contexts()) for (const page of context.pages()) {
      if (!page.isClosed()) await page.evaluate(() => document.getElementById('reset')?.click());
    }
    await browser.close();
  }
})().catch(error => { console.error(error); process.exitCode = 1; });
