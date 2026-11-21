'use strict';

const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const publish = require('../../.github/workflows/publish-ota.js');

const sha = '0123456789abcdef0123456789abcdef01234567';
const short = sha.slice(0, 12);
const app = Buffer.from('application');
const full = Buffer.from('full-flash');
const digest = data => crypto.createHash('sha256').update(data).digest('hex');
const files = new Map([
  [`BYAI_firmware_${short}.bin`, app],
  [`BYAI_full_flash_${short}.bin`, full],
  [`BYAI_SHA256SUMS_${short}.txt`, Buffer.from(`${digest(app)}  BYAI_firmware_${short}.bin\n${digest(full)}  BYAI_full_flash_${short}.bin\n`)],
  ['BYAI_OTA.json', Buffer.from(JSON.stringify({schema_version: 1, project: 'BYAI', chip: 'esp32s3', source: 'dev', ref: 'dev', commit: sha, version: `dev-${short}`, application: {name: `BYAI_firmware_${short}.bin`, size: app.length, sha256: digest(app)}}))],
]);

async function main() {
  const previous = process.cwd();
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'byai-release-test-'));
  try {
    process.chdir(directory);
    fs.mkdirSync('ota-artifact');
    for (const [name, data] of files) fs.writeFileSync(path.join('ota-artifact', name), data);
    Object.assign(process.env, {OTA_SOURCE_SHA: sha, OTA_APP_VERSION: `dev-${short}`, OTA_REF_NAME: 'dev', OTA_REF_TYPE: 'branch'});
    let release;
    let tagExists = false;
    let moved = false;
    let created = 0;
    let published = 0;
    const assets = [];
    const github = {
      rest: {
        git: {
          getRef: async () => {
            if (!tagExists) { const error = new Error('missing'); error.status = 404; throw error; }
            return {data: {object: {type: 'commit', sha: moved ? 'f'.repeat(40) : sha}}};
          },
          createRef: async () => { tagExists = true; },
        },
        repos: {
          getReleaseByTag: async () => {
            if (!release || release.draft) { const error = new Error('missing'); error.status = 404; throw error; }
            return {data: release};
          },
          listReleases: async () => ({data: release ? [release] : []}),
          createRelease: async opts => {
            created++;
            release = {id: 1, tag_name: opts.tag_name, prerelease: opts.prerelease, draft: true};
            return {data: release};
          },
          listReleaseAssets: async () => ({data: assets}),
          uploadReleaseAsset: async opts => {
            const asset = {id: assets.length + 1, name: opts.name, size: opts.data.length, state: 'uploaded', bytes: opts.data};
            assets.push(asset);
            return {data: asset};
          },
          getReleaseAsset: async opts => ({data: assets.find(asset => asset.id === opts.asset_id).bytes}),
          updateRelease: async () => { published++; release.draft = false; },
        },
      },
      paginate: async fn => (await fn()).data,
    };
    const args = {github, context: {repo: {owner: 'test', repo: 'BYAI'}}, core: {notice() {}}};
    await publish(args);
    assert.equal(created, 1);
    assert.equal(published, 1);
    assert.equal(assets.length, 4);
    await publish(args);
    assert.equal(created, 1);
    assert.equal(published, 1);
    assets[0].bytes = Buffer.from('different');
    await assert.rejects(publish(args), /digest mismatch/);
    assets[0].bytes = app;
    moved = true;
    await assert.rejects(publish(args), /no longer points/);
    moved = false;
    release.draft = true;
    assets.pop();
    await publish(args);
    assert.equal(created, 1);
    assert.equal(published, 2);
    assert.equal(assets.length, 4);
    console.log('Release publication checks passed');
  } finally {
    process.chdir(previous);
    fs.rmSync(directory, {recursive: true, force: true});
  }
}

main().catch(error => { console.error(error); process.exitCode = 1; });
