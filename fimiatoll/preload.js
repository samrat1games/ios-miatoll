// The renderer gets only these calls: no Node, no shell.
const { contextBridge, ipcRenderer } = require('electron');

contextBridge.exposeInMainWorld('fi', {
  platform: process.platform,
  tools: () => ipcRenderer.invoke('tools'),
  devices: () => ipcRenderer.invoke('devices'),
  pick: (kind) => ipcRenderer.invoke('pick', kind),
  inspectIpsw: (file) => ipcRenderer.invoke('ipsw:inspect', file),
  repo: () => ipcRenderer.invoke('repo'),
  exists: (f) => ipcRenderer.invoke('exists', f),
  buildCommand: (ipsw, work) => ipcRenderer.invoke('ipsw:command', ipsw, work),
  openTerminal: (cmd) => ipcRenderer.invoke('terminal', cmd),
  flash: (what, file) => ipcRenderer.invoke('flash', what, file),
  backupBoot: (file) => ipcRenderer.invoke('backup:boot', file),
  reboot: (target) => ipcRenderer.invoke('reboot', target),
  bundled: (look) => ipcRenderer.invoke('bundled', look),
  ipaCheck: (f) => ipcRenderer.invoke('ipa:check', f),
  ipaServe: (fs_) => ipcRenderer.invoke('ipa:serve', fs_),
  ipaStop: () => ipcRenderer.invoke('ipa:stop'),
  openLink: (url) => ipcRenderer.invoke('link', url),
  onLog: (cb) => ipcRenderer.on('log', (_e, line) => cb(line)),
  onProgress: (cb) => ipcRenderer.on('progress', (_e, p) => cb(p)),
});
