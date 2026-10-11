#ifndef DISCOVERY_H
#define DISCOVERY_H
// Owned JSON descriptions. Native discovery runs foreign code in a separate process.
char *scan_plugins(const char *path);
void scan_plugins_clear(void);
// Owned path to native discovery preferences; NULL when the platform has no local catalog.
char *plugin_settings_path(void);
#endif
