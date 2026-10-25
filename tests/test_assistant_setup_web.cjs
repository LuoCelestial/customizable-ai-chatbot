/* Run the actual embedded page script with a small DOM/HTTP harness. */
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const html = fs.readFileSync(path.join(__dirname, '../main/setup.html'), 'utf8');
const source = html.match(/<script>([\s\S]*?)<\/script>/)[1];

function page(state, holdWifi = false) {
    const elements = {};
    for (const match of html.matchAll(/<[^>]+\bid="([^"]+)"[^>]*>/g)) {
        elements[match[1]] = {
            value: match[0].match(/\bvalue="([^"]*)"/)?.[1] || '',
            checked: /\bchecked\b/.test(match[0]), disabled: false, hidden: false,
            files: [], dataset: {}, style: {setProperty() {}},
        };
    }
    elements.mode.value = 'direct';
    const posts = [];
    const context = vm.createContext({
        document: {getElementById: id => elements[id], querySelectorAll: () => [],
                   documentElement: {style: {setProperty() {}}}},
        location: {protocol: 'http:'}, TextEncoder, URL,
        FormData: class { *[Symbol.iterator]() { yield ['mode', 'direct']; } },
        fetch(url, options = {}) {
            if (url === '/wifi' && holdWifi) return new Promise(() => {});
            if (options.method === 'POST') posts.push({url, data: JSON.parse(options.body)});
            const value = url === '/wifi' ? state : {};
            return Promise.resolve({ok: true, json: () => Promise.resolve(value)});
        },
    });
    vm.runInContext(source, context);
    return {elements, posts, run: code => vm.runInContext(code, context)};
}

(async () => {
    const pending = page({}, true);
    assert.throws(() => pending.run('addBackupSettings({})'), /读取/);
    const saved = page({saved: true, backup_enabled: true, backup_ssid: 'backup-test'});
    await new Promise(setImmediate);
    assert.equal(saved.elements.backup_enabled.checked, true);
    assert.equal(saved.elements.backup_new_fields.disabled, true);
    assert.equal(saved.elements.backup_wifi_password.value, '');
    await saved.elements.form.onsubmit({preventDefault() {}, submitter: saved.elements.wifi_save});
    assert.equal(saved.posts[0].url, '/configure');
    assert.equal(saved.posts[0].data.wifi_only, true);
    assert.equal(saved.posts[0].data.use_saved_wifi, true);
    assert.equal(saved.posts[0].data.use_saved_backup, true);
    assert.equal(saved.posts[0].data.backup_enabled, true);

    const updated = page({saved: true, backup_enabled: true, backup_ssid: 'backup-test'});
    await new Promise(setImmediate);
    updated.elements.backup_change.onclick();
    updated.elements.backup_ssid.value = 'new-backup';
    updated.elements.backup_wifi_password.value = 'test-password';
    const data = updated.run('addBackupSettings({})');
    assert.equal(data.use_saved_backup, false);
    assert.equal(data.backup_ssid, 'new-backup');
    assert.equal(data.backup_wifi_password, 'test-password');
    updated.elements.backup_wifi_password.value = 'short';
    assert.throws(() => updated.run('addBackupSettings({})'), /密码/);
    updated.elements.backup_wifi_password.value = '';
    updated.elements.backup_ssid.value = '中'.repeat(11);
    assert.throws(() => updated.run('addBackupSettings({})'), /32/);
    updated.elements.backup_enabled.checked = false;
    updated.elements.backup_enabled.onchange();
    assert.equal(updated.run('addBackupSettings({})').backup_enabled, false);
    assert.equal(updated.elements.backup_fields.disabled, true);
    console.log('Embedded setup page: saved backup, replacement, removal, limits and load race PASS');
})().catch(error => { console.error(error); process.exitCode = 1; });
