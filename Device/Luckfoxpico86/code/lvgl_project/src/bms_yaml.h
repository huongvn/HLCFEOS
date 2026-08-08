#ifndef BMS_YAML_H
#define BMS_YAML_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BMS_YAML_FILE "devices.yaml"

bool bms_yaml_load(const char *path);
bool bms_yaml_write_default(const char *path);

#ifdef __cplusplus
}
#endif

#endif