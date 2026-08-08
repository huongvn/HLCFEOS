#include "src/bms_yaml.h"
#include "src/bms.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdbool.h>

/*==================
 * LINE READER
 *==================*/
#define MAX_LINE 256

typedef struct {
    char key[64];
    char val[128];
    int  indent;
    bool is_list;
} yaml_kv_t;

/*==================
 * STRING TRIM
 *==================*/
static char* trim_space(char *s) {
    while (isspace((unsigned char)*s)) s++;
    if (*s == '\0') return s;
    char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) end--;
    *(end + 1) = '\0';
    return s;
}

static char* strip_quotes(char *s) {
    size_t len = strlen(s);
    if (len >= 2 && s[0] == '"' && s[len - 1] == '"') {
        s[len - 1] = '\0';
        return s + 1;
    }
    return s;
}

/*==================
 * LINE PARSE: "key: value" or "- key: value"
 *==================*/
static bool parse_line(const char *raw, yaml_kv_t *kv) {
    kv->indent = 0;
    kv->key[0] = '\0';
    kv->val[0] = '\0';
    kv->is_list = false;

    const char *p = raw;
    while (*p == ' ') { kv->indent++; p++; }
    if (*p == '\0' || *p == '#' || *p == '\r' || *p == '\n') return false;

    if (*p == '-') {
        kv->is_list = true;
        p++;
        while (*p == ' ') p++;
    }

    const char *colon = strchr(p, ':');
    if (!colon) {
        strncpy(kv->val, p, sizeof(kv->val) - 1);
        return true;
    }

    size_t klen = colon - p;
    if (klen >= sizeof(kv->key)) klen = sizeof(kv->key) - 1;
    memcpy(kv->key, p, klen);
    kv->key[klen] = '\0';

    char tmp_key[64];
    strncpy(tmp_key, kv->key, sizeof(tmp_key) - 1);
    char *tk = trim_space(tmp_key);
    strncpy(kv->key, tk, sizeof(kv->key) - 1);

    // Also strip quotes from key if present (for nested attrs like "0110": ...)
    kv->key[63] = '\0';

    const char *vstart = colon + 1;
    while (*vstart == ' ') vstart++;
    strncpy(kv->val, vstart, sizeof(kv->val) - 1);

    return true;
}

/*==================
 * VALUE PARSERS
 *==================*/
static int yaml_int(const char *s) {
    return atoi(trim_space((char *)s));
}

static bool yaml_bool(const char *s) {
    char buf[32];
    strncpy(buf, s, sizeof(buf) - 1);
    char *t = trim_space(buf);
    return strcmp(t, "true") == 0 || strcmp(t, "TRUE") == 0 || strcmp(t, "1") == 0;
}

static const char* yaml_str(const char *s) {
    return strip_quotes(trim_space((char *)s));
}

static dev_type_t parse_type(const char *v) {
    // LVGL type values
    if (strcmp(v, "ac") == 0) return DEV_TYPE_AC;
    if (strcmp(v, "sign") == 0) return DEV_TYPE_SIGN;
    if (strcmp(v, "switch") == 0) return DEV_TYPE_SWITCH;
    if (strcmp(v, "power_meter") == 0) return DEV_TYPE_POWER_METER;
    if (strcmp(v, "light_sensor") == 0) return DEV_TYPE_LIGHT_SENSOR;
    // Node-RED nr_type values (khi YAML chi co nr_type, khong co type)
    if (strcmp(v, "ac_controller") == 0) return DEV_TYPE_AC;
    if (strcmp(v, "mcb") == 0) return DEV_TYPE_SIGN;
    if (strcmp(v, "switch") == 0) return DEV_TYPE_SWITCH;
    if (strcmp(v, "power_meter") == 0) return DEV_TYPE_POWER_METER;
    if (strcmp(v, "light_sensor") == 0) return DEV_TYPE_LIGHT_SENSOR;
    return DEV_TYPE_AC;
}

/*==================
 * DEVICE BUILDER
 *==================*/
static bms_device_t dev_default(void) {
    bms_device_t d;
    memset(&d, 0, sizeof(d));
    d.device_id = 0;
    d.type = DEV_TYPE_AC;
    d.online = false;
    d.enabled = false;
    d.value = 0;
    d.room_temp = 0;
    d.mode = 0;
    d.fan_speed = 0;
    d.lux_value = 0;
    d.lux_on_thresh = 300;
    d.lux_off_thresh = 800;
    d.ac_index = 0;
    d.sign_index = 0;
    d.switch_index = 0;
    return d;
}

static void dev_apply_kv(bms_device_t *d, const char *key, const char *val) {
    char clean_key[64];
    strncpy(clean_key, key, sizeof(clean_key) - 1);
    char *ck = strip_quotes(trim_space(clean_key));

    if (strcmp(ck, "device_id") == 0)  d->device_id = yaml_int(val);
    else if (strcmp(ck, "type") == 0)  d->type = parse_type(trim_space((char *)val));
    else if (strcmp(ck, "nr_type") == 0)  d->type = parse_type(trim_space((char *)val));
    else if (strcmp(ck, "name") == 0)  strncpy(d->name, yaml_str(val), DEV_NAME_LEN - 1);
    else if (strcmp(ck, "group") == 0) strncpy(d->group, yaml_str(val), sizeof(d->group) - 1);
    else if (strcmp(ck, "ac_index") == 0)    d->ac_index = yaml_int(val);
    else if (strcmp(ck, "sign_index") == 0)  d->sign_index = yaml_int(val);
    else if (strcmp(ck, "power_index") == 0)  d->power_index = yaml_int(val);
    else if (strcmp(ck, "light_sensor_index") == 0)  d->light_sensor_index = yaml_int(val);
    else if (strcmp(ck, "switch_index") == 0) d->switch_index = yaml_int(val);
    // silently ignore unknown keys (nr_type, zigbee_addr, gateway, attrs, etc.)
}

/*==================
 * LOAD
 *==================*/
bool bms_yaml_load(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) {
        printf("[BMS_YAML] File %s not found\n", path);
        return false;
    }

    g_max_devices = MAX_DEVICES_DEFAULT;
    bool in_devices = false;
    bool in_attrs   = false;
    int  devices_list_indent = -1;
    int  attr_list_indent = -1;
    int  decode_list_indent = -1;
    bms_device_t temp_dev;
    memset(&temp_dev, 0, sizeof(temp_dev));
    bool building_dev = false;
    char cur_attr_id[32] = "";
    char cur_attr_label[32] = "";
    char cur_attr_unit[16] = "";
    bool cur_attr_display = false;
    bool cur_attr_overview = true;
    bool cur_attr_control  = false;
    bms_attr_type_t cur_attr_type = ATTR_TYPE_NUMBER;
    bool in_decode = false;

    char line[MAX_LINE];
    while (fgets(line, sizeof(line), f)) {
        yaml_kv_t kv;
        if (!parse_line(line, &kv)) continue;

        if (!in_devices) {
            if (strcmp(kv.key, "max_devices") == 0) {
                g_max_devices = yaml_int(kv.val);
                if (g_max_devices < 1) g_max_devices = MAX_DEVICES_DEFAULT;
            } else if (strcmp(kv.key, "devices") == 0) {
                in_devices = true;
                devices_list_indent = -1;
            }
            continue;
        }

        // ── Device boundary detection ──
        char clean_key[64];
        if (kv.key[0]) {
            strncpy(clean_key, kv.key, sizeof(clean_key) - 1);
            char *tk = strip_quotes(trim_space(clean_key));
            strncpy(kv.key, tk, sizeof(kv.key) - 1);
        }

        if (kv.is_list && kv.key[0] != '\0') {
            if (devices_list_indent < 0) {
                devices_list_indent = kv.indent;
            }
            if (kv.indent == devices_list_indent) {
                // ── New device ──
                // Commit pending attr from previous device (e.g., decode sub-item with display:true)
                if (cur_attr_display && cur_attr_id[0] && temp_dev.display_attr_count < MAX_DISPLAY_ATTRS) {
                    strncpy(temp_dev.display_attr_ids[temp_dev.display_attr_count], cur_attr_id, 31);
                    strncpy(temp_dev.display_attr_labels[temp_dev.display_attr_count], cur_attr_label, 31);
                    strncpy(temp_dev.display_attr_units[temp_dev.display_attr_count], cur_attr_unit, 15);
                    temp_dev.display_attr_types[temp_dev.display_attr_count] = cur_attr_type;
                    temp_dev.display_attr_overview[temp_dev.display_attr_count] = cur_attr_overview;
                    temp_dev.display_attr_control[temp_dev.display_attr_count]  = cur_attr_control;
                    temp_dev.display_attr_count++;
                }
                cur_attr_id[0] = '\0';
                cur_attr_label[0] = '\0';
                cur_attr_unit[0] = '\0';
                cur_attr_display = false;
                cur_attr_overview = true;
                cur_attr_control  = false;
                cur_attr_type = ATTR_TYPE_NUMBER;
                // Commit previous device
                if (building_dev && g_device_count < g_max_devices) {
                    g_devices[g_device_count] = temp_dev;
                    g_device_count++;
                }
                memset(&temp_dev, 0, sizeof(temp_dev));
                temp_dev = dev_default();
                building_dev = true;
                in_attrs = false;
                in_decode = false;
                dev_apply_kv(&temp_dev, kv.key, kv.val);
            } else if (in_attrs && kv.indent == attr_list_indent) {
                // ── New attribute inside attributes: ──
                if (cur_attr_display && cur_attr_id[0] && temp_dev.display_attr_count < MAX_DISPLAY_ATTRS) {
                    strncpy(temp_dev.display_attr_ids[temp_dev.display_attr_count], cur_attr_id, 31);
                    strncpy(temp_dev.display_attr_labels[temp_dev.display_attr_count], cur_attr_label, 31);
                    strncpy(temp_dev.display_attr_units[temp_dev.display_attr_count], cur_attr_unit, 15);
                    temp_dev.display_attr_types[temp_dev.display_attr_count] = cur_attr_type;
                    temp_dev.display_attr_overview[temp_dev.display_attr_count] = cur_attr_overview;
                    temp_dev.display_attr_control[temp_dev.display_attr_count]  = cur_attr_control;
                    temp_dev.display_attr_count++;
                }
                cur_attr_id[0] = '\0';
                cur_attr_label[0] = '\0';
                cur_attr_unit[0] = '\0';
                cur_attr_display = false;
                cur_attr_overview = true;
                cur_attr_control  = false;
                cur_attr_type = ATTR_TYPE_NUMBER;
                in_decode = false;
                if (strcmp(kv.key, "id") == 0) {
                    strncpy(cur_attr_id, yaml_str(kv.val), 31);
                }
            } else if (in_decode && kv.indent == decode_list_indent) {
                // ── New decode sub-item ──
                if (cur_attr_display && cur_attr_id[0] && temp_dev.display_attr_count < MAX_DISPLAY_ATTRS) {
                    strncpy(temp_dev.display_attr_ids[temp_dev.display_attr_count], cur_attr_id, 31);
                    strncpy(temp_dev.display_attr_labels[temp_dev.display_attr_count], cur_attr_label, 31);
                    strncpy(temp_dev.display_attr_units[temp_dev.display_attr_count], cur_attr_unit, 15);
                    temp_dev.display_attr_types[temp_dev.display_attr_count] = cur_attr_type;
                    temp_dev.display_attr_overview[temp_dev.display_attr_count] = cur_attr_overview;
                    temp_dev.display_attr_control[temp_dev.display_attr_count]  = cur_attr_control;
                }
                cur_attr_id[0] = '\0';
                cur_attr_label[0] = '\0';
                cur_attr_unit[0] = '\0';
                cur_attr_display = false;
                cur_attr_overview = true;
                cur_attr_control  = false;
                cur_attr_type = ATTR_TYPE_NUMBER;
                if (strcmp(kv.key, "id") == 0) {
                    strncpy(cur_attr_id, yaml_str(kv.val), 31);
                }
            }
        } else if (!kv.is_list && kv.key[0] != '\0' && building_dev) {
            if (strcmp(kv.key, "attributes") == 0) {
                // ── Enter attributes section ──
                in_attrs = true;
                in_decode = false;
                attr_list_indent = kv.indent + 2;
                cur_attr_id[0] = '\0';
                cur_attr_display = false;
            } else if (strcmp(kv.key, "decode") == 0) {
                // ── Enter decode sub-section ──
                in_decode = true;
                decode_list_indent = kv.indent + 2;
                cur_attr_id[0] = '\0';
                cur_attr_display = false;
            } else if (in_decode && strcmp(kv.key, "display") == 0) {
                cur_attr_display = yaml_bool(kv.val);
            } else if (in_attrs && !in_decode && strcmp(kv.key, "display") == 0) {
                cur_attr_display = yaml_bool(kv.val);
            } else if (in_decode && strcmp(kv.key, "overview") == 0) {
                cur_attr_overview = yaml_bool(kv.val);
            } else if (in_attrs && !in_decode && strcmp(kv.key, "overview") == 0) {
                cur_attr_overview = yaml_bool(kv.val);
            } else if (in_decode && strcmp(kv.key, "control") == 0) {
                cur_attr_control = yaml_bool(kv.val);
            } else if (in_attrs && !in_decode && strcmp(kv.key, "control") == 0) {
                cur_attr_control = yaml_bool(kv.val);
            } else if (in_attrs && !in_decode && strcmp(kv.key, "id") == 0) {
                strncpy(cur_attr_id, yaml_str(kv.val), 31);
            } else if (in_attrs && !in_decode && strcmp(kv.key, "label") == 0) {
                strncpy(cur_attr_label, yaml_str(kv.val), 31);
            } else if (in_decode && strcmp(kv.key, "label") == 0) {
                strncpy(cur_attr_label, yaml_str(kv.val), 31);
            } else if (in_attrs && !in_decode && strcmp(kv.key, "unit") == 0) {
                strncpy(cur_attr_unit, yaml_str(kv.val), 15);
            } else if (in_decode && strcmp(kv.key, "unit") == 0) {
                strncpy(cur_attr_unit, yaml_str(kv.val), 15);
            } else if (in_attrs && !in_decode && strcmp(kv.key, "type") == 0) {
                cur_attr_type = (strcmp(trim_space(kv.val), "bool") == 0) ? ATTR_TYPE_BOOL : ATTR_TYPE_NUMBER;
            } else if (in_decode && strcmp(kv.key, "type") == 0) {
                cur_attr_type = (strcmp(trim_space(kv.val), "bool") == 0) ? ATTR_TYPE_BOOL : ATTR_TYPE_NUMBER;
            } else {
                dev_apply_kv(&temp_dev, kv.key, kv.val);
            }
        }
    }

    // commit last attr
    if (cur_attr_display && cur_attr_id[0] && temp_dev.display_attr_count < MAX_DISPLAY_ATTRS) {
        strncpy(temp_dev.display_attr_ids[temp_dev.display_attr_count], cur_attr_id, 31);
        strncpy(temp_dev.display_attr_labels[temp_dev.display_attr_count], cur_attr_label, 31);
        strncpy(temp_dev.display_attr_units[temp_dev.display_attr_count], cur_attr_unit, 15);
        temp_dev.display_attr_types[temp_dev.display_attr_count] = cur_attr_type;
        temp_dev.display_attr_overview[temp_dev.display_attr_count] = cur_attr_overview;
        temp_dev.display_attr_control[temp_dev.display_attr_count]  = cur_attr_control;
        temp_dev.display_attr_count++;
    }

    // commit last device
    if (building_dev && g_device_count < g_max_devices) {
        g_devices[g_device_count] = temp_dev;
        g_device_count++;
    }

    fclose(f);

    printf("[BMS_YAML] Loaded %d devices, max=%d\n", g_device_count, g_max_devices);
    return g_device_count > 0;
}

/*==================
 * WRITE DEFAULT
 *==================*/
bool bms_yaml_write_default(const char *path) {
    FILE *f = fopen(path, "w");
    if (!f) {
        perror("[BMS_YAML] Failed to write default config");
        return false;
    }

    fprintf(f, "site: \"bluCafe\"\n");
    fprintf(f, "max_devices: %d\n", MAX_DEVICES_DEFAULT);
    fprintf(f, "devices:\n");
    fprintf(f, "  - device_id: 1\n");
    fprintf(f, "    zigbee_addr: \"0x0000\"\n");
    fprintf(f, "    nr_type: ac_controller\n");
    fprintf(f, "    name: \"AC Khu A\"\n");
    fprintf(f, "    group: \"Khu A\"\n");
    fprintf(f, "    gateway: \"tasmota_CHANGE_ME\"\n");
    fprintf(f, "    ac_index: 0\n");
    fprintf(f, "    enabled: true\n");
    fprintf(f, "    attributes:\n");
    fprintf(f, "      - id: \"0101\"\n");
    fprintf(f, "        label: \"Power\"\n");
    fprintf(f, "        type: bool\n");
    fprintf(f, "        display: true\n");
    fprintf(f, "        xsolar: true\n");
    fprintf(f, "        xsolar_key: \"power\"\n");
    fprintf(f, "      - id: \"0202\"\n");
    fprintf(f, "        label: \"Temperature\"\n");
    fprintf(f, "        type: number\n");
    fprintf(f, "        unit: \"°C\"\n");
    fprintf(f, "        display: true\n");
    fprintf(f, "        xsolar: true\n");
    fprintf(f, "        xsolar_key: \"temperature\"\n");
    fprintf(f, "      - id: \"0203\"\n");
    fprintf(f, "        label: \"Room Temp\"\n");
    fprintf(f, "        type: number\n");
    fprintf(f, "        unit: \"°C\"\n");
    fprintf(f, "        display: true\n");
    fprintf(f, "        xsolar: true\n");
    fprintf(f, "        xsolar_key: \"room_temp\"\n");
    fprintf(f, "      - id: \"0405\"\n");
    fprintf(f, "        label: \"Fan Speed\"\n");
    fprintf(f, "        type: number\n");
    fprintf(f, "        display: true\n");
    fprintf(f, "        xsolar: true\n");
    fprintf(f, "        xsolar_key: \"fan\"\n");
    fprintf(f, "  - device_id: 2\n");
    fprintf(f, "    zigbee_addr: \"0x0001\"\n");
    fprintf(f, "    nr_type: mcb\n");
    fprintf(f, "    name: \"Bien QC\"\n");
    fprintf(f, "    group: \"Ngoai\"\n");
    fprintf(f, "    gateway: \"tasmota_CHANGE_ME\"\n");
    fprintf(f, "    sign_index: 0\n");
    fprintf(f, "    enabled: true\n");
    fprintf(f, "    attributes:\n");
    fprintf(f, "      - id: \"0110\"\n");
    fprintf(f, "        label: \"Control\"\n");
    fprintf(f, "        type: bool\n");
    fprintf(f, "        display: true\n");
    fprintf(f, "        xsolar: true\n");
    fprintf(f, "        xsolar_key: \"control\"\n");
    fprintf(f, "      - id: \"0201\"\n");
    fprintf(f, "        label: \"Energy\"\n");
    fprintf(f, "        type: number\n");
    fprintf(f, "        scale: 0.01\n");
    fprintf(f, "        unit: \"kWh\"\n");
    fprintf(f, "        display: true\n");
    fprintf(f, "        xsolar: true\n");
    fprintf(f, "        xsolar_key: \"current_energy\"\n");
    fprintf(f, "      - id: \"0006\"\n");
    fprintf(f, "        label: \"Raw composite\"\n");
    fprintf(f, "        type: hex\n");
    fprintf(f, "        display: false\n");
    fprintf(f, "        xsolar: false\n");
    fprintf(f, "        decode:\n");
    fprintf(f, "          - id: \"current_power\"\n");
    fprintf(f, "            label: \"Công suất\"\n");
    fprintf(f, "            type: number\n");
    fprintf(f, "            unit: \"W\"\n");
    fprintf(f, "            display: true\n");
    fprintf(f, "            xsolar: true\n");
    fprintf(f, "            xsolar_key: \"current_power\"\n");
    fprintf(f, "            slice: [10, 16]\n");

    fclose(f);
    printf("[BMS_YAML] Created default config: %s\n", path);
    return true;
}