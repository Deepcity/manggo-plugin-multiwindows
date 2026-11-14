import { contextBridge, ipcRenderer } from "electron";

contextBridge.exposeInMainWorld("multiwindows", {
  getSession: () => ipcRenderer.invoke("session:get"),
  getBridgeConfig: () => ipcRenderer.invoke("bridge:get-config"),
  copyText: (text: string) => ipcRenderer.invoke("clipboard:copy", text),
  createWindow: () => ipcRenderer.send("window:create"),
  closeWindow: () => ipcRenderer.send("window:close"),
  setAlwaysOnTop: (enabled: boolean) => ipcRenderer.invoke("window:set-always-on-top", enabled),
});
