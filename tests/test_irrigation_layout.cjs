// Render the embedded HTML/CSS without contacting a device. Dates are test fixtures.
const fs = require('fs');
const {chromium} = require('playwright');
const assert = require('node:assert/strict');
(async () => {
  const payload = path => fs.readFileSync(path, 'utf8').split('R"rawliteral(')[1].split(')rawliteral";')[0];
  const css = payload('src/style_css.h');
  const html = payload('src/index_html.h').replace(/%[A-Z_]+%/g, '')
    .replace(/<script[\s\S]*?<\/script>/g, '').replace(/<link[^>]*>/g, '')
    .replace('</title>', '</title><style>' + css + '</style>');
  fs.mkdirSync('layout-previews', {recursive: true});
  const browser = await chromium.launch({headless: true});
  const page = await browser.newPage();
  await page.setContent(html);
  await page.evaluate(() => {
    document.querySelectorAll('.page').forEach(el => el.classList.remove('active'));
    document.getElementById('runsettings').classList.add('active');
    document.querySelectorAll('#irrigationSettingsSection,#irrigationParameterSection').forEach(el => el.style.display = '');
    document.getElementById('localPlanEditor').disabled = false;
    document.getElementById('localPlanLast').value = '2026-11-12';
  });
  for (const width of [1640, 1000, 390]) {
    await page.setViewportSize({width, height: 1000});
    await page.waitForTimeout(350); // Finish existing sidebar/page transitions.
    const card = await page.locator('#irrigationSettingsSection').boundingBox();
    const params = await page.locator('#irrigationParameterSection').boundingBox();
    const row = await page.locator('.runsettings-operations-row').boundingBox();
    await page.screenshot({path: `layout-previews/irrigation-layout-${width}.png`, fullPage: true});
    assert.ok(card.y + card.height <= params.y + 1, 'Settings must be above parameters');
    assert.ok(Math.abs(card.width - row.width) < 2, 'Settings must span the full row');
    assert.ok(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth + 1), 'No horizontal page overflow');
    console.log(`${width}px: full-width settings above parameters; no horizontal overflow`);
  }
  await browser.close();
})().catch(error => {console.error(error);process.exit(1);});
