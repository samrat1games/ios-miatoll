// FiMiatoll (Flash-iMiatoll): main process. Talks to adb/fastboot, reads IPSWs, never runs anything the UI types.
const { app, BrowserWindow, ipcMain, dialog, shell } = require('electron');
const path = require('path');
const fs = require('fs');
const os = require('os');
const { spawn, execFile } = require('child_process');
const AdmZip = require('adm-zip');

let win;

function tool(name) {
  const exe = process.platform === 'win32' ? name + '.exe' : name;
  const candidates = [
    path.join(process.resourcesPath || '', 'platform-tools', exe),
    path.join(__dirname, 'platform-tools', exe),
  ];
  for (const c of candidates) if (fs.existsSync(c)) return c;
  return name; // from PATH
}

function run(file, args, timeout = 8000) {
  return new Promise((resolve) => {
    execFile(tool(file), args, { timeout }, (err, stdout, stderr) =>
      resolve({ ok: !err, out: String(stdout || ''), err: String(stderr || (err && err.message) || '') }));
  });
}

function log(line) { if (win && !win.isDestroyed()) win.webContents.send('log', line); }
function progress(p) { if (win && !win.isDestroyed()) win.webContents.send('progress', p); }

// ---- streaming runner (only called with commands built below, never with UI text) ----
function stream(file, args) {
  return new Promise((resolve) => {
    log('$ ' + path.basename(file) + ' ' + args.join(' '));
    const child = spawn(tool(file), args);
    const onData = (d) => {
      for (const line of String(d).split(/\r?\n/)) {
        if (!line.trim()) continue;
        log(line);
        const m = line.match(/(\d+)\/(\d+)/); // "Sending sparse 'userdata' 3/25"
        if (m) progress(Math.round((+m[1] / +m[2]) * 100));
      }
    };
    child.stdout.on('data', onData);
    child.stderr.on('data', onData);
    child.on('error', (e) => { log('error: ' + e.message); resolve(false); });
    child.on('close', (code) => resolve(code === 0));
  });
}

// ---- IPC ----
ipcMain.handle('tools', async () => {
  const f = await run('fastboot', ['--version']);
  const a = await run('adb', ['version']);
  return { fastboot: f.ok, adb: a.ok, fastbootInfo: f.out.split('\n')[0], adbInfo: a.out.split('\n')[0] };
});

ipcMain.handle('devices', async () => {
  const f = await run('fastboot', ['devices']);
  const a = await run('adb', ['devices']);
  const list = [];
  for (const l of f.out.split('\n')) { const m = l.trim().match(/^(\S+)\s+fastboot/); if (m) list.push({ id: m[1], mode: 'fastboot' }); }
  for (const l of a.out.split('\n').slice(1)) {
    const m = l.trim().match(/^(\S+)\s+(device|recovery|sideload|unauthorized)$/);
    if (m) list.push({ id: m[1], mode: m[2] === 'device' ? 'android' : m[2] });
  }
  return list;
});

ipcMain.handle('pick', async (_e, kind) => {
  const filters = kind === 'ipsw' ? [{ name: 'IPSW', extensions: ['ipsw', 'zip'] }]
    : kind === 'simg' ? [{ name: 'Sparse root image', extensions: ['simg', 'img'] }]
    : [{ name: 'Boot image', extensions: ['img'] }];
  const r = await dialog.showOpenDialog(win, { properties: ['openFile'], filters });
  return r.canceled ? null : r.filePaths[0];
});

ipcMain.handle('ipsw:inspect', async (_e, file) => {
  try {
    const zip = new AdmZip(file);
    const names = zip.getEntries().map((e) => e.entryName);
    const mf = zip.getEntry('BuildManifest.plist');
    if (!mf) return { ok: false, reason: 'noManifest' };
    const text = mf.getData().toString('utf8');
    const version = (text.match(/<key>ProductVersion<\/key>\s*<string>([^<]+)</) || [])[1] || '?';
    const build = (text.match(/<key>ProductBuildVersion<\/key>\s*<string>([^<]+)</) || [])[1] || '?';
    const types = [...text.matchAll(/<string>(iPhone\d+,\d+)<\/string>/g)].map((m) => m[1]);
    const supported = types.some((t) => t === 'iPhone10,3' || t === 'iPhone10,6') && /^16\.7/.test(version);
    const dmgs = names.filter((n) => n.endsWith('.dmg'));
    return { ok: true, version, build, types: [...new Set(types)], supported, dmgs: dmgs.length };
  } catch (e) {
    return { ok: false, reason: 'unreadable', detail: String(e.message || e) };
  }
});

// The root of iOS is built by the repository's tools (apfs-fuse / HFS+ need macOS or Linux). The app lives in <repo>/fimiatoll.
function repoDir() {
  const r = path.join(__dirname, '..');
  return fs.existsSync(path.join(r, 'tools', 'ios-install.sh')) ? r : null;
}
ipcMain.handle('repo', async () => repoDir());
ipcMain.handle('exists', async (_e, file) => { try { return fs.statSync(file).size; } catch { return 0; } });

ipcMain.handle('ipsw:command', async (_e, ipsw, work) => {
  const q = (s) => "'" + String(s).replace(/'/g, "'\\''") + "'";
  if (process.platform === 'win32') return { ok: false, reason: 'windows' };
  const repo = repoDir();
  if (!repo) return { ok: false, reason: 'norepo' };
  const script = process.platform === 'darwin' ? 'ios-install.sh' : 'ios-install-linux.sh';
  const w = work || path.join(os.homedir(), 'ios-work');
  return { ok: true, work: w, simg: path.join(w, 'ios16-root.simg'), cmd: `cd ${q(repo)} && tools/${script} ${q(ipsw)} ${q(w)}` };
});

ipcMain.handle('terminal', async (_e, cmd) => {
  if (typeof cmd !== 'string' || /[\n\r]/.test(cmd)) return false;
  if (process.platform === 'darwin') {
    const script = `tell application "Terminal" to do script ${JSON.stringify(cmd)}\ntell application "Terminal" to activate`;
    spawn('osascript', ['-e', script]);
    return true;
  }
  if (process.platform === 'linux') {
    spawn('x-terminal-emulator', ['-e', 'bash', '-lc', cmd + '; read -p "Enter to close"']);
    return true;
  }
  return false;
});

// what the app may flash: fixed commands, files chosen in the file dialog
ipcMain.handle('flash', async (_e, what, file) => {
  progress(0);
  let ok = false;
  if (what === 'boot') ok = await stream('fastboot', ['flash', 'boot', file]);
  else if (what === 'userdata') ok = await stream('fastboot', ['flash', '-S', '512M', 'userdata', file]);
  else return false;
  progress(ok ? 100 : 0);
  return ok;
});

ipcMain.handle('backup:boot', async (_e, file) => {
  return new Promise((resolve) => {
    log('backup: adb exec-out dd boot -> ' + file);
    const out = fs.createWriteStream(file);
    const child = spawn(tool('adb'), ['exec-out', 'dd if=/dev/block/by-name/boot bs=1M 2>/dev/null']);
    child.stdout.pipe(out);
    child.on('error', () => resolve(false));
    child.on('close', (code) => { out.end(); resolve(code === 0 && fs.statSync(file).size > 1e6); });
  });
});

ipcMain.handle('reboot', async (_e, target) => {
  const allowed = { system: ['reboot'], bootloader: ['reboot', 'bootloader'], recovery: ['reboot', 'recovery'] };
  if (!allowed[target]) return false;
  const f = await run('fastboot', allowed[target], 15000);
  if (f.ok) return true;
  const a = await run('adb', allowed[target], 15000);
  return a.ok;
});

ipcMain.handle('bundled', async (_e, look) => {
  if (!/^(sf|google|classic)$/.test(look)) return null;
  const f = [path.join(process.resourcesPath || '', 'images', `shim-boot-${look}.img`), path.join(__dirname, 'images', `shim-boot-${look}.img`)]
    .find((p) => fs.existsSync(p));
  return f || null;
});

ipcMain.handle('link', async (_e, url) => {
  if (/^https:\/\/(developer\.android\.com|github\.com)\//.test(url)) { await shell.openExternal(url); return true; }
  return false;
});

function createWindow() {
  win = new BrowserWindow({
    width: 460, height: 860, minWidth: 420, minHeight: 700,
    title: 'FiMiatoll',
    titleBarStyle: process.platform === 'darwin' ? 'hiddenInset' : 'default',
    backgroundColor: '#f2f2f7',
    webPreferences: { preload: path.join(__dirname, 'preload.js'), contextIsolation: true, nodeIntegration: false, sandbox: true },
  });
  win.setMenuBarVisibility(false);
  win.loadFile(path.join(__dirname, 'renderer', 'index.html'));
  const shot = process.env.FIMIATOLL_SHOT;
  if (shot) {
    win.webContents.on('did-finish-load', () => setTimeout(async () => {
      const js = process.env.FIMIATOLL_JS;
      if (js) { try { await win.webContents.executeJavaScript(js); } catch (e) { console.error(e); } await new Promise((r) => setTimeout(r, 600)); }
      const img = await win.webContents.capturePage();
      fs.writeFileSync(shot, img.toPNG());
      app.quit();
    }, 900));
  }
}

app.whenReady().then(createWindow);
app.on('window-all-closed', () => app.quit());
