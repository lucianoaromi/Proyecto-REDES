/* Prueba opcional de desarrollo; no es una dependencia de la aplicación. */
'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { pathToFileURL } = require('node:url');
const { chromium } = require(process.env.PLAYWRIGHT_MODULE_PATH || 'playwright');

(async () => {
  const root = path.resolve(__dirname, '..');
  const output = process.env.VISUALIZATION_TEST_OUTPUT || path.join(require('node:os').tmpdir(), 'redes-visualizacion-tests');
  fs.mkdirSync(output, { recursive: true });
  const browser = await chromium.launch({ channel: process.env.VISUALIZATION_BROWSER || 'msedge', headless: true });
  let checks = 0;
  const check = (condition, message) => { assert.ok(condition, message); checks++; };
  try {
    const context = await browser.newContext({ viewport: { width: 1440, height: 1050 }, offline: true });
    const page = await context.newPage(), errors = [], remote = [];
    page.on('pageerror', error => errors.push(error.message));
    page.on('console', message => { if (message.type() === 'error') errors.push(message.text()); });
    page.on('request', request => { if (/^https?:/.test(request.url())) remote.push(request.url()); });
    await page.goto(pathToFileURL(path.join(root, 'index.html')).href);
    await page.clock.install();
    check(await page.title() === 'Proyecto-REDES · Arquitectura en movimiento', 'abre desde file:// sin red');
    check(await page.locator('[data-scenario]').count() === 8, 'ocho escenarios');
    check(await page.locator('#view-network').isVisible(), 'vista inicial de red');

    await page.locator('#play').click();
    await page.clock.runFor(1200);
    const packet = page.locator('#network-packets .packet').first();
    const position = await packet.getAttribute('transform');
    await page.clock.runFor(300);
    check(position !== await packet.getAttribute('transform'), 'el paquete recorre la ruta SVG');
    await page.locator('#play').click();
    const frozen = await packet.getAttribute('transform');
    await page.clock.runFor(1500);
    check(frozen === await packet.getAttribute('transform'), 'pausa congela el paquete');
    check(await page.locator('body').getAttribute('data-playback') === 'paused', 'pausa de reproducción');
    await page.locator('#play').click();
    // Ejecutar el primer RAF de la continuación antes de saltar el reloj.
    await page.clock.runFor(32);
    // Avance del reloj simulado: no supone duraciones fijas por escenario.
    await page.clock.fastForward(5 * 60 * 1000);
    check(await page.locator('body').getAttribute('data-playback') === 'complete', 'avanza y finaliza automáticamente');
    await page.locator('#play').click();
    check(await page.locator('#step-counter').textContent() === '01', 'repetir vuelve al primer paso');
    await page.locator('#reset').click();
    check(await page.locator('#step-counter').textContent() === '00', 'reiniciar vuelve a listo');
    check(await page.locator('#network-packets .packet').count() === 0, 'reiniciar limpia partículas');

    for (const scenario of ['http', 'led', 'weather', 'telegram', 'wifi', 'portal', 'ntp', 'general']) {
      await page.locator(`[data-scenario="${scenario}"]`).click();
      const count = await page.locator('#timeline button').count();
      check(count > 2, `${scenario}: etapas disponibles`);
      for (let i = 0; i < count; i++) {
        await page.locator('#next').click();
        check(await page.locator('#step-counter').textContent() === String(i + 1).padStart(2, '0'), `${scenario}: paso ${i + 1}`);
        check((await page.locator('#info-protocol').textContent()).length > 0, `${scenario}: ficha de protocolo`);
        const values = await page.locator('.packet').evaluateAll(nodes => nodes.map(n => n.getAttribute('transform')));
        check(values.every(v => !v || !/NaN|undefined/.test(v)), `${scenario}: geometría válida`);
      }
      await page.locator('#next').click();
      check(await page.locator('body').getAttribute('data-playback') === 'complete', `${scenario}: completar por pasos`);
    }

    await page.locator('[data-scenario="led"]').click();
    await page.locator('#tab-hardware').click();
    for (const command of ['on', 'off', 'auto']) {
      await page.locator(`[data-led="${command}"]`).click();
      await page.locator('#timeline button').nth(2).click();
      const d6 = await page.locator('[data-led-node="d6"]').getAttribute('class');
      const d7 = await page.locator('[data-led-node="d7"]').getAttribute('class');
      check(d6.split(' ').includes('on') === (command !== 'off'), `D6 ${command}`);
      check(d7.split(' ').includes('on') === (command === 'on'), `D7 ${command}`);
      await page.locator('#timeline button').nth(0).click();
      check(!(await page.locator('#hardware-packets .packet').count()), 'retroceso reconstruye la etapa');
    }
    await page.locator('[data-led="on"]').click();
    await page.locator('#timeline button').nth(1).click();
    check((await page.locator('#info-operation').textContent()).includes('PUT /led?mode=manual&on=1'), 'endpoint manual exacto');
    await page.locator('#tab-network').click();
    await page.locator('#client').selectOption('phone');
    await page.locator('#timeline button').nth(1).click();
    check(await page.locator('#info-from').textContent() === 'Teléfono móvil', 'selector de cliente');
    check(await page.locator('#link-phone-ap').getAttribute('class').then(v => v.includes('is-active')), 'ruta desde teléfono');

    await page.locator('[data-scenario="portal"]').click();
    await page.locator('#timeline button').nth(3).click();
    check((await page.locator('#message').textContent()).includes('192.168.4.1'), 'IP documentada solo del portal');
    check((await page.locator('#network-svg').getAttribute('class')).includes('portal-active'), 'enlace directo del portal');
    await page.locator('#timeline button').last().click();
    check(!(await page.locator('#network-svg').getAttribute('class')).includes('portal-active'), 'portal cerrado al habilitar HTTP habitual');
    await page.locator('#network-svg [data-node="ap"]').focus();
    await page.keyboard.press('Enter');
    check(await page.locator('#node-title').textContent() === 'Router / punto de acceso', 'inspector accesible por teclado');
    await page.locator('#tab-network').focus();
    await page.keyboard.press('ArrowRight');
    check(await page.locator('#view-hardware').isVisible(), 'tabs con flechas');
    await page.locator('#tab-network').click();
    await page.locator('#reset').click();
    await page.locator('#speed').fill('2');
    check(await page.locator('#speed-value').textContent() === '2×', 'selector de velocidad');

    await page.locator('[data-scenario="http"]').click();
    await page.locator('#timeline button').first().click();
    check(await page.locator('.wan.is-active').count() === 0, 'HTTP local no recorre Internet');
    await page.locator('#timeline button').nth(2).click();
    check((await page.locator('#info-port').textContent()).includes('efímero'), 'respuesta HTTP al puerto del cliente');
    await page.locator('[data-scenario="weather"]').click();
    await page.locator('#timeline button').nth(4).click();
    check((await page.locator('#message').textContent()).includes('HTTP/1.0'), 'WeatherAPI usa HTTP/1.0');
    check(await page.locator('#info-port').textContent() === 'TCP 443', 'WeatherAPI destino 443');
    await page.locator('[data-scenario="telegram"]').click();
    await page.locator('#timeline button').nth(4).click();
    check((await page.locator('#message').textContent()).includes('GET …/sendMessage HTTP/1.1'), 'Telegram usa GET HTTP/1.1');
    await page.locator('[data-scenario="ntp"]').click();
    await page.locator('#timeline button').nth(2).click();
    check(await page.locator('#info-port').textContent() === 'UDP 123', 'NTP usa UDP 123');

    for (const width of [320, 390, 768, 1280, 1440, 1920]) {
      await page.setViewportSize({ width, height: width === 1920 ? 1080 : 1000 });
      const sizes = await page.evaluate(() => ({ body: document.documentElement.scrollWidth, width: innerWidth }));
      check(sizes.body <= sizes.width + 1, `sin desborde de página a ${width}px`);
      for (const view of ['network', 'hardware']) {
        await page.locator(`#tab-${view}`).click();
        check(await page.locator(`#${view}-svg`).isVisible(), `${width}px: ${view} visible`);
        const valid = await page.locator(`#${view}-svg text`).evaluateAll(nodes => nodes.every(node => {
          if (node.closest('.packet')?.style.display === 'none') return true;
          const box = node.getBBox(), area = node.ownerSVGElement.viewBox.baseVal;
          const transform = node.ownerSVGElement.getScreenCTM().inverse().multiply(node.getScreenCTM());
          const start = new DOMPoint(box.x, box.y).matrixTransform(transform);
          const end = new DOMPoint(box.x + box.width, box.y + box.height).matrixTransform(transform);
          return start.x >= -1 && start.y >= -1 && end.x <= area.width + 1 && end.y <= area.height + 1;
        }));
        check(valid, `${width}px: etiquetas dentro del SVG`);
      }
    }
    await page.setViewportSize({ width: 1920, height: 1080 });
    await page.locator('[data-scenario="led"]').click();
    await page.locator('[data-led="on"]').click();
    await page.locator('#timeline button').nth(2).click();
    await page.screenshot({ path: path.join(output, 'hardware-1920.png'), fullPage: true });
    await page.locator('#tab-network').click();
    await page.locator('[data-scenario="http"]').click();
    await page.locator('#reset').click();
    await page.locator('#fullscreen').click();
    check(await page.evaluate(() => !!document.fullscreenElement), 'botón abre pantalla completa');
    const presentation = await page.evaluate(() => ({
      height: innerHeight, page: document.documentElement.scrollHeight,
      controls: document.getElementById('speed').getBoundingClientRect().bottom,
      title: document.querySelector('.intro').getBoundingClientRect().height
    }));
    check(presentation.page <= presentation.height + 1, 'proyección 1920×1080 sin scroll vertical');
    check(presentation.controls < presentation.height && presentation.title === 0, 'controles visibles en modo exposición');
    await page.screenshot({ path: path.join(output, 'proyeccion-1920.png') });
    await page.locator('#fullscreen').click();
    await page.evaluate(() => { document.activeElement.blur(); window.scrollTo(0, 0); });
    await page.screenshot({ path: path.join(output, 'red-1920.png'), fullPage: true });
    await page.setViewportSize({ width: 390, height: 844 });
    await page.screenshot({ path: path.join(output, 'movil-390.png'), fullPage: true });

    const reduced = await browser.newContext({ reducedMotion: 'reduce', offline: true });
    const reducedPage = await reduced.newPage();
    await reducedPage.goto(pathToFileURL(path.join(root, 'index.html')).href);
    await reducedPage.clock.install();
    await reducedPage.locator('#play').click();
    await reducedPage.clock.runFor(1200);
    const fixed = await reducedPage.locator('.packet').first().getAttribute('transform');
    await reducedPage.clock.runFor(900);
    check(fixed === await reducedPage.locator('.packet').first().getAttribute('transform'), 'respeta movimiento reducido');
    check((await fs.promises.readFile(path.join(root, 'index.html'), 'utf8')).includes("connect-src 'none'"), 'CSP prohíbe conexiones');
    check(remote.length === 0, 'ninguna solicitud HTTP/HTTPS externa');
    check(errors.length === 0, `sin errores de JavaScript ni consola: ${errors.join('; ')}`);
    console.log(JSON.stringify({ result: 'OK', checks, viewports: [320, 390, 768, 1280, 1440, 1920], browser: 'Chromium / Edge', offline: true, screenshots: output }, null, 2));
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
