/* Invoked by actions/github-script with an artifact from this workflow run. */
const fs = require('fs');
const crypto = require('crypto');
const path = require('path');

module.exports = async ({ github, context, core }) => {
  const { owner, repo } = context.repo;
  const sourceSha = process.env.OTA_SOURCE_SHA;
  const version = process.env.OTA_APP_VERSION;
  const ref = process.env.OTA_REF_NAME;
  const refType = process.env.OTA_REF_TYPE;
  if (!/^[a-f0-9]{40}$/.test(sourceSha)) throw new Error('Invalid source SHA');
  const source = refType === 'branch' ? ref : 'tag';
  if (!['dev', 'main', 'tag'].includes(source)) throw new Error('Invalid source');
  const tagName = source === 'tag' ? ref : `ota-${source}-${sourceSha}`;
  if (source === 'tag' && /^ota-(dev|main)-/.test(tagName)) throw new Error('Reserved tag');
  const shortSha = sourceSha.slice(0, 12);
  const names = [`BYAI_firmware_${shortSha}.bin`, `BYAI_full_flash_${shortSha}.bin`,
    `BYAI_SHA256SUMS_${shortSha}.txt`, 'BYAI_OTA.json'];
  const content = new Map(names.map(name => [name, fs.readFileSync(path.join('ota-artifact', name))]));
  const hash = data => crypto.createHash('sha256').update(data).digest('hex');
  const manifest = JSON.parse(content.get('BYAI_OTA.json').toString('utf8'));
  if (manifest.schema_version !== 1 || manifest.project !== 'BYAI' || manifest.chip !== 'esp32s3' ||
      manifest.source !== source || manifest.ref !== ref || manifest.commit !== sourceSha ||
      manifest.version !== version || manifest.application?.name !== names[0] ||
      manifest.application?.size !== content.get(names[0]).length ||
      manifest.application?.sha256 !== hash(content.get(names[0]))) {
    throw new Error('OTA manifest and build identity differ');
  }
  const sums = content.get(names[2]).toString('utf8');
  const expectedSums = `${hash(content.get(names[0]))}  ${names[0]}\n${hash(content.get(names[1]))}  ${names[1]}\n`;
  if (sums !== expectedSums) throw new Error('SHA256SUMS differs from build artifacts');

  async function resolveTag() {
    let object = (await github.rest.git.getRef({ owner, repo, ref: `tags/${tagName}` })).data.object;
    const visited = new Set();
    for (let depth = 0; object.type === 'tag'; depth++) {
      if (depth >= 8 || visited.has(object.sha)) throw new Error('Tag nesting is invalid');
      visited.add(object.sha);
      object = (await github.rest.git.getTag({ owner, repo, tag_sha: object.sha })).data.object;
    }
    if (object.type !== 'commit' || object.sha !== sourceSha) throw new Error('Tag no longer points to built commit');
  }
  try {
    await resolveTag();
  } catch (error) {
    if (source === 'tag' || error.status !== 404) throw error;
    try {
      await github.rest.git.createRef({ owner, repo, ref: `refs/tags/${tagName}`, sha: sourceSha });
    } catch (creationError) {
      if (creationError.status !== 422) throw creationError;
    }
    await resolveTag();
  }

  let release;
  try {
    release = (await github.rest.repos.getReleaseByTag({ owner, repo, tag: tagName })).data;
  } catch (error) {
    if (error.status !== 404) throw error;
    const drafts = (await github.paginate(github.rest.repos.listReleases,
      { owner, repo, per_page: 100 })).filter(item => item.draft && item.tag_name === tagName);
    if (drafts.length > 1) throw new Error('Multiple drafts exist for release tag');
    release = drafts[0];
    if (!release) {
      release = (await github.rest.repos.createRelease({
        owner, repo, tag_name: tagName, target_commitish: sourceSha,
        name: `${source} ${version}`, draft: true, prerelease: source !== 'tag', make_latest: 'false',
      })).data;
    }
  }
  await resolveTag();
  if (release.tag_name !== tagName || release.prerelease !== (source !== 'tag')) {
    throw new Error('Existing release identity differs');
  }
  const assets = await github.paginate(github.rest.repos.listReleaseAssets,
    { owner, repo, release_id: release.id, per_page: 100 });
  if (assets.some(asset => !names.includes(asset.name)) || new Set(assets.map(asset => asset.name)).size !== assets.length) {
    throw new Error('Existing release has unexpected or duplicate assets');
  }
  for (const name of names) {
    let asset = assets.find(item => item.name === name);
    if (!asset) {
      if (!release.draft) throw new Error(`Published release is missing ${name}`);
      asset = (await github.rest.repos.uploadReleaseAsset({
        owner, repo, release_id: release.id, name, data: content.get(name),
        headers: { 'content-type': 'application/octet-stream', 'content-length': content.get(name).length },
      })).data;
    }
    if (asset.state !== 'uploaded' || asset.size !== content.get(name).length) {
      throw new Error(`Release asset size/state mismatch: ${name}`);
    }
    const downloaded = await github.rest.repos.getReleaseAsset({
      owner, repo, asset_id: asset.id, headers: { accept: 'application/octet-stream' },
    });
    const bytes = Buffer.from(downloaded.data);
    if (hash(bytes) !== hash(content.get(name))) throw new Error(`Release asset digest mismatch: ${name}`);
  }
  await resolveTag();
  if (release.draft) {
    await github.rest.repos.updateRelease({
      owner, repo, release_id: release.id, draft: false, prerelease: source !== 'tag', make_latest: 'false',
    });
  }
  core.notice(`OTA release ${tagName} verified for ${sourceSha}`);
};
