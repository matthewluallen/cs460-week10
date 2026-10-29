// Minimal Node-RED settings for the lab HMI station. No editor auth (lab only).
module.exports = {
  uiPort: process.env.PORT || 1880,
  flowFile: 'flows.json',
  flowFilePretty: true,
  ui: { path: 'ui' },                         // dashboard served at /ui
  functionGlobalContext: {},
  exportGlobalContextKeys: false,
  editorTheme: { projects: { enabled: false }, tours: false },
  logging: { console: { level: 'info', metrics: false, audit: false } },
};
