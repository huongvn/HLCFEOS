# HISTORY

Danh sách lịch sử chi tiết của 395 commit trước ngày rút gọn repo (squash vào commit root mới).

```
339ca18 2026-03-09 screen red ok
8b3d6cc 2026-03-10 release v1.0
4295e14 2026-03-10 update submodule version
e28eb03 2026-03-10 Remove unused lv_drivers dependency
cf05ba8 2026-03-10 Update README.md with comprehensive documentation
2d5f42e 2026-03-10 Add QWEN.md - Comprehensive project context documentation
6fce735 2026-03-11 add DOC folder
4ca3bb9 2026-03-12 release 1.1
8efb739 2026-03-13 Add touch button counter feature
64d60a6 2026-03-13 Add Master Panel with DND/MUR buttons
90efe94 2026-03-13 Add NanoMQ HTTP API integration
2524544 2026-03-13 Add interlock logic for DND/MUR buttons
4f3fe23 2026-03-13 Update README with Master Panel and MQTT docs
a5cc5cc 2026-03-17 Refactor UI into modular structure with Smart Room Controller
382fa7b 2026-03-17 Adjust Smart Room layout
a158ee8 2026-03-17 Smart Room: Replace Back button with clickable Canopi logo
975bf79 2026-03-17 Smart Room: Improve Fan Slider and Eco button layout
c2f609c 2026-03-17 Smart Room: Reduce Fan Slider and Eco button height
9d4962c 2026-03-17 Remove Master Panel and improve Temperature Arc touch area
4b82a21 2026-03-17 Fix Fan Slider layout to show 100% correctly
3497834 2026-03-17 Smart Room: Replace Canopi text with logo_med image
049efe7 2026-03-17 Update logo_med.c with new design
22467f3 2026-03-17 Fix logo name and rebuild app
d063484 2026-03-17 Smart Room: Add date display next to time
243d369 2026-03-17 Smart Room: Left-align time/date with Room and Welcome
3df9662 2026-03-17 Smart Room: Add sensor icons and MQTT subscribe
0aa6058 2026-03-17 Smart Room: Fix sensor layout and remove strange characters
319995a 2026-03-17 Smart Room: Add unit symbols to sensor values
3f2db6e 2026-03-17 Add real MQTT subscribe support with MQTT-C library
2e013a2 2026-03-17 Fix MQTT subscribe: Move to smart_room.c for UI updates
e62657b 2026-03-17 Debug MQTT: Add detailed logging and always call mqtt_sync
6fbe429 2026-03-17 Fix MQTT receive: Non-blocking socket + debug logs
699bbde 2026-03-17 Fix MQTT non-blocking connect: Handle EINPROGRESS properly
bcb300a 2026-03-17 Smart Room: Change background to dark teal #083731
d137c3a 2026-03-17 Update COLOR_ROOM_DARK to #1A4943
fb7af33 2026-03-17 Smart Room: Update Temperature Arc layout and add Occupied status
aefb793 2026-03-17 Debug MQTT: Enhanced logging with hex dump and fallback matching
36b62d5 2026-03-17 Debug MQTT: Add subscription logging and socket health check
51ddc86 2026-03-17 Fix MQTT: Change client ID to 'pico-lvgl' to avoid conflict
d71c993 2026-03-17 Debug occupied_handler: Add detailed logging
fbef398 2026-03-17 Fix occupied_handler: Handle whitespace, NULL, and case sensitivity
393547c 2026-03-17 Simplify occupied_handler: Only accept 'true' and 'false'
6ff1fe9 2026-03-17 Fix occupied_handler crash: Add comprehensive safety checks
57da539 2026-03-17 CRITICAL FIX: Move LVGL calls from MQTT callback to timer
9a6082d 2026-03-17 Refactor MQTT stack: Migrate from mqtt-c to libmosquitto
4f6e9c6 2026-03-18 UI: Add new icons for mode and eco, update layouts
dae3201 2026-03-18 UI: Refine DND and MMR buttons logic, colors, and layout
4ede565 2026-03-18 Code review: fix 15 issues (Critical/Major/Medium), update docs
bbfe361 2026-03-18 UI: Add MQTT-driven room info labels
6a899ab 2026-03-18 UI: Increase temperature font sizes
c47c96e 2026-03-18 Update doc folder file
19f3c7d 2026-03-18 UI: Add Power button with MQTT integration
196abe0 2026-03-18 UI: Add remote control via MQTT topics
3db24c8 2026-03-18 docs: Add MQTT_API.md for easy sharing of API specs
1e136cf 2026-03-18 UI: Implement MQTT configurable screen sleep and wake
b7cfd9b 2026-03-18 perf: Optimize screen wake speed with direct backlight control
253fbc8 2026-03-18 UI: Remove rounded corners from Smart Room page
7a5ec4e 2026-03-18 UI: Rotate temperature arc to make the gap face downwards
3add1b9 2026-03-18 UI: Reshape temp arc to 180 degrees and increase thickness
7c6d8db 2026-03-18 UI: Increase Temperature Arc size by 2x
9bc59e3 2026-03-18 UI: Optimize Temperature Arc size and positioning to prevent layout overlap
333840e 2026-03-18 UI: Disable scrollbars on temp arc container to fix overflow issue
0ea6f88 2026-03-19 feat: add settings screen with pin auth and persistent config
b816b20 2026-03-19 feat: redesign smart room header and add temp +/- buttons
dd739d2 2026-03-19 docs: update README, QWEN, MQTT_API with settings and UI redesign
bc403f7 2026-03-19 style: restyle settings and PIN pages to match smart room theme
534758b 2026-03-19 feat: add screen timeout, restyle system monitor
f464cd5 2026-03-19 update doc
e74263e 2026-03-19 feat: add Vietnamese language support and fix UI bugs
f1b6523 2026-03-19 fix: set default screen to Smart Room and finish translations
0cfb929 2026-03-19 docs: update README roadmap
dc7ceb9 2026-03-24 Implement safe OTA update system with systemd support and automated build packaging
2a70851 2026-03-24 update doc ota
1d8724f 2026-03-24 Implement Smart Room state persistence and MQTT synchronization on startup
3cfc327 2026-03-25 Update README.md and QWEN.md with Smart Room persistence and Static IP details
05f32b0 2026-03-25 Implement AC Power interlock UI effect (dimmed overlay)
fdb5448 2026-03-25 Expand AC Power interlock overlay to Fan Slider and set opacity to 90%
26d5a4f 2026-03-25 Reduce AC power interlock overlay opacity to 20%
b6f21ea 2026-03-25 Fix static IP logic: kill DHCP client before applying static settings
24c18c1 2026-03-25 Enhance Static IP logic with additional routing variants and diagnostics
f7b131d 2026-03-25 Aggressive network application and diagnostics
ad2bee7 2026-03-25 Add startup delay and aggressive network application for static IP persistence
dcaced9 2026-03-25 Fully revert Static IP feature and update documentation
59eadbe 2026-03-25 Cleanup settings.c: Remove Network UI and logic
3a77d4a 2026-03-25 Adjust Smart Room overlay (full width, no radius) and fix QWEN.md duplication
411b3a7 2026-03-25 Fix overlay Z-index and height: now covers Eco and bottom buttons
e2d07e5 2026-03-25 Disable smart room scrollbars and adjust overlay height
c399a32 2026-04-02 feat: Add Smart Light page with 4 lights and 2 curtains control
6c06257 2026-04-02 fix(smart_light): Use smart_room_mqtt_publish for MQTT integration
63c53fe 2026-04-02 feat(smart_light): Update UI layout and button sizes for better usability
758e520 2026-04-02 feat: Smart Light persistence and UI improvements
d25e284 2026-04-02 feat: Smart Room logo navigation improvements
66f967f 2026-04-02 feat(smart_light): Update UI style to match Smart Room theme
299701b 2026-04-08 test upload
2cb58ae 2026-05-17 feat: init project with Smart Cafe Energy Management slides
a7155f2 2026-05-17 fix(slide-01): remove target metrics and audience sections from title slide
bfbc378 2026-05-17 fix(slide-01): update architecture diagram labels
00daad3 2026-05-17 refactor(slide-02): replace ROI content with comprehensive tech stack overview
b5e5f6d 2026-05-17 rename(slide-02): rename 02-roi.md to 02-techstack.md
28fcc33 2026-05-18 fix(slide-02): connect LVGL directly to NanoMQ instead of Node-RED
5da34f1 2026-05-18 fix(slide-02,05): correct Zigbee mesh topology and device roles
df8ac9b 2026-05-18 fix(slide-02): redraw Zigbee network as star topology
9b12bed 2026-05-18 fix(slide-02): Smart Panel contains NanoMQ, Node-RED, LVGL HMI
5dc1d23 2026-05-18 fix(slides): update architecture and terminology
30bbd9e 2026-05-18 fix(slides): refine content based on user edits
c3cb179 2026-05-18 fix(slides): minor updates to scalability, zigbee, watchdog content
ad9f602 2026-05-18 fix(slide-05): fix mermaid syntax error in zigbee diagram
c1ad56c 2026-05-21 umload
736a7e5 2026-05-22 docs(agents): sync AGENTS.md with actual slide structure
13c71c2 2026-06-25 doc and file for flash fiwmware for ZB-gw03 v1.4
6e192f4 2026-06-25 feat: add lvgl_project submodule for Luckfox Core1106 Smart 86 Box
02f212c 2026-06-25 docs: hướng dẫn chuyển Luckfox WiFi AP sang Client mode
2435c8a 2026-06-25 docs: hướng dẫn chuyển WiFi AP sang Client mode
c04a38c 2026-06-26 feat: add WiFi connection settings in Network tab
85195b5 2026-06-26 feat: add WiFi connection settings in Network tab (lvgl_project submodule update)
f94220e 2026-06-26 feat: add BMS dashboard with 3 screens (Overview, AC, Light), MQTT integration, navbar, compact cards
112f789 2026-06-26 feat: BMS dashboard - 3 screens with MQTT integration, compact cards, navbar
782ef5b 2026-06-26 fix: BMS dashboard - remove status bar, config panel, scene buttons MQTT only, fix temp buttons, navbar center, AC card layout with room temp
e030340 2026-06-26 fix: BMS dashboard layout improvements
e15e551 2026-06-26 fix: BMS light screen remove sub-header, sign card height 160px, AC button alignment
2bb93ce 2026-06-26 fix: BMS light & AC screen layout improvements
ddb310a 2026-06-26 feat: dark theme colors for BMS, summary bar MQTT topics, load BMS on startup
1b0935b 2026-06-26 feat: BMS dark theme, summary MQTT, default startup
8995d58 2026-06-26 fix: clock long-press wrapper, dark theme, Blu Coffee branding
98031a8 2026-06-26 fix: clock long-press wrapper for System Monitor navigation
3390a52 2026-06-26 refactor: extract MQTT to mqtt_broker, remove smart_room/smart_light, dark theme
79db695 2026-06-26 refactor: extract MQTT to mqtt_broker, remove smart_room/smart_light pages, apply dark theme to all UI, fix header branding
0cc8309 2026-06-26 feat: increase AC card temp buttons to 64px, temp font to 48, add *C unit label, card height 300px
0200131 2026-06-26 feat: enlarge AC card temp controls (64px buttons, 48px font, *C unit, 300px card height)
c89411e 2026-06-26 feat: BMS multi-language support, signal dot, scene master topic, temp button disable & pressed state, sign card layout match AC
170e5a4 2026-06-26 feat: BMS i18n, signal dots, scene topic, temp button UX improvements, sign card restyle
63923ec 2026-06-27 fix: wifi connect flush UI before blocking nmcli, verify connection with grep after success
7914415 2026-06-27 fix: WiFi connection UX - flush UI before nmcli, verify real connection, better status messages
03acf87 2026-06-30 feat: Node-RED BMS flows - data-driven 5-layer architecture
cf5a352 2026-06-30 fix: update gateway hostname tasmota-6DCAA8-2728-eth -> tasmota_6DCAA8
b2d0eab 2026-06-30 fix: add enabled field in Startup fn-load-config
ac5235a 2026-06-30 fix: SQLite params use msg.params instead of msg.payload
d57262f 2026-06-30 fix: replace SQL ? placeholders with direct esc() interpolation
0b03236 2026-06-30 fix: ui-form property formElements -> form (Dashboard 2.0 compat)
80b4435 2026-06-30 fix: set topic for ui-switch-manual
ec7532f 2026-06-30 fix: replace ui-form with individual ui-text-input + ui-dropdown + ui-switch (Dashboard 2.0 no ui-form)
d19bbb3 2026-06-30 fix: repair link connections + match real Dashboard group IDs
22b2862 2026-06-30 fix: L3 link_in IDs match actual dashboard (ff930e67, 09a3dc14)
5b6bace 2026-06-30 fix: ui-table explicit column definitions, disable autocols
c535369 2026-06-30 fix: add tab-l5 to flow-only file, group IDs match user dashboard exactly
4af3532 2026-06-30 fix: ui-table data format -> array of arrays (Dashboard 2.0 compat)
54cecd6 2026-06-30 fix: table back to autocols + array of objects to avoid append bug
b0a97ee 2026-06-30 fix: wrap table data as {rows: [...]} to force replace instead of append
a9b732c 2026-06-30 fix: table clear-first strategy - send empty array then data
924d679 2026-06-30 fix: correct Node-RED return syntax for 2-message clear+data
99da0b1 2026-06-30 fix: L2->L5 real-time link + delay debounce for table replace
acf95b5 2026-06-30 fix: replace ui-table with ui-template (HTML table) to avoid append bug in Dashboard 2.0 v1.30.2
4e71bc3 2026-06-30 fix: replace ui-template with ui-markdown - reliable table rendering, no append
7010c68 2026-06-30 fix: ui-table with msg.reset=true to force replace (not append)
12a3603 2026-06-30 fix: ui-template with flat HTML v-for (no line breaks in template)
1a54bbf 2026-06-30 fix: remove optional chaining in ui-template v-for
a0996c0 2026-06-30 debug: ui-template test - display raw JSON to verify msg arrives
2170a1f 2026-06-30 debug: ui-template minimal test - {{msg.payload}}
ddcfc2f 2026-06-30 debug: add debug node + v-if/v-else ui-template to diagnose empty payload vs msg scope
50adaaa 2026-06-30 fix: revert to ui-table (autocols), remove delay + debug; ui-template msg scope broken in v1.30.2
10f612d 2026-06-30 fix: ui-table clear-then-populate workaround — send [] then data
2b3a0c5 2026-06-30 debug: add debug nodes for manual control (dropdown + ZbSend command)
aa212a6 2026-06-30 fix: Tasmota ZbSend format — use Device (cap D) + Write {EF00/XXXX: val} instead of nested send/Cluster
ac18555 2026-06-30 feat: ui-switch reflects real device state via L2 feedback; decouple=true prevents command loop
1215d7c 2026-06-30 fix: ui-switch decouple=false — instant UI update, no loading spinner; passthru=false prevents command loop
f5938db 2026-06-30 fix: sync-switch reads power_attr from device_config.extra; fallback to raw_XXXX for unmapped attrs
bd3ebf2 2026-06-30 feat: add Device Detail card on Devices page — dropdown + full info text display with auto-refresh
a6ed1a1 2026-06-30 feat: time-series metric storage + dashboard chart\n\n- Fix L2 state merge (04-l2): preserve attributes across partial MQTT messages\n- Add L3 metric flow (05-l3): insert all state attrs into device_metric table\n- Add Dashboard chart group (07-l5): dropdown device+metric, SVG line chart, auto-refresh
4e1ece0 2026-06-30 feat: metric display shows all metrics for selected device (no metric dropdown)
5e31914 2026-06-30 feat: metric display as plain text list (no chart, no table)
d2e2346 2026-06-30 fix: metric query use correlated subquery; add debug node
d9938d8 2026-06-30 fix: fn-l3-metrics use single INSERT with multiple VALUES (SQLite doesn't support multi-statement)
ffceb25 2026-06-30 fix: link L2→L3 metrics — add lk-l3-metrics-in to lk-l2-to-l3 links
d777749 2026-06-30 fix: metric display use ui-table with array objects (reliable rendering)
9d0bb44 2026-06-30 feat: map AC EF00/0203 as ambient_temp (environment temperature)
f766b08 2026-06-30 fix: metric display as ui-text with HTML <br> — each metric on separate line
2365b6b 2026-06-30 fix: ui-table-log revert from ui-template to ui-table (autocols) — ui-template msg scope broken in v1.30.2
d308b2f 2026-06-30 fix: log table - define columns, auto-width payload, clear-then-populate to prevent append
a2d9bb7 2026-06-30 feat: add JSON pretty-print preview below log table — shows latest log details
1414ffb 2026-06-30 feat: complete HMI Bridge with LVGL topic mapping\n\n- Publish status: bms/ac/N/{temperature,room_temp,power,mode,online}\n- Publish status: bms/light/N/{power,online}, bms/sign/N/{power,online}\n- Publish summary: bms/status/{ac_on_count,ac_avg_temp,lt_on_count}\n- Subscribe set: bms/ac/N/power/set -> ZbSend POWER_ON/OFF\n- Subscribe set: bms/ac/N/temperature/set -> ZbSend AC_SET temp\n- Subscribe set: bms/ac/N/mode/set -> ZbSend AC_SET mode\n- Subscribe set: bms/light/N/power/set, bms/sign/N/power/set\n- Subscribe scene: bms/scene/master
3937ade 2026-06-30 feat: HMI Bridge AC-only + fix AC seed 0405->fan_speed\n\n- Publish: bms/ac/N/{temperature,room_temp,power,fan,online}\n- Publish: bms/status/{ac_on_count,ac_avg_temp}\n- Subscribe: bms/ac/N/{power,temperature,fan}/set -> ZbSend\n- Removed: scene/master, light/*, sign/*, mode topic
7e94b31 2026-06-30 chore: remove 01-db-init.json — DB now managed manually via sqlite3 CLI
09f8871 2026-06-30 docs: add init_database_over_sqlite3.md — manual DB setup guide
7b74c95 2026-06-30 docs: update MCB seed with guessed attribute names (control, rated_current, high_voltage_cutoff, etc.)
f27cc68 2026-06-30 docs: add section 4 - reset database (drop + recreate + seed)
ad73565 2026-06-30 docs: replace AC 0x8150 with 0xC5A9 (zone_C) and 0x336A (zone_D)
1ef3b40 2026-06-30 docs: add explicit DROP TABLE step at start of Section 4
b39b86e 2026-06-30 docs: rewrite init_database_over_sqlite3.md — full guide from DROP TABLE to seed new devices
3e79f27 2026-06-30 fix: HMI Bridge sort AC list by device_id for fixed LVGL mapping\n\n- 0x336A (zone_D) -> index 0 (Khu A)\n- 0xC5A9 (zone_C) -> index 1 (Khu B)
3296303 2026-06-30 fix: HMI Bridge optimistic update + 2s tick\n\n- Tick: 10s -> 2s (faster feedback)\n- Optimistic update: set global state immediately when receiving set command\n- Trigger publish 500ms after set to push new state to LVGL before MQTT feedback arrives
3de536f 2026-06-30 fix: HMI Bridge remove trigger, add debug incoming MQTT, tick back to 10s
11fe456 2026-06-30 fix: HMI Bridge remove optimistic update — only reflect real MQTT feedback, not immediate echo
68dae05 2026-06-30 debug: add acIds logging to trace why ZbSend not sent
09172fe 2026-06-30 fix: HMI Bridge ZbSend format — add EF00/ prefix to Write keys
9d0fd3d 2026-06-30 fix: ui-table-log autocols=true, columns=[] — manual columns may cause blank cells
381d721 2026-06-30 fix: HMI parse-set idx scope — declare outside if blocks
78cd6cd 2026-06-30 feat: HMI Bridge simplified — send-only ON/OFF, no status feedback to LVGL
00abd61 2026-06-30 fix: HMI Bridge add back temperature/set and fan/set handling
07d2389 2026-06-30 fix: HMI Bridge fix wiring — remove dead dbg-hmi-in reference, add debug output
5d90458 2026-06-30 fix: HMI temperature only sends temp attribute (Tasmota doesn't support multi-attr)
516e63a 2026-06-30 fix: HMI fan only sends fan_speed attribute (Tasmota doesn't support multi-attr)
66be8e5 2026-06-30 fix: all timestamps use Asia/Ho_Chi_Minh timezone (GMT+7) instead of UTC
ef5f028 2026-06-30 feat: increase log table limit 100 -> 500 rows
c3eb261 2026-06-30 chore: remove Metric Chart group and all related widgets from Dashboard
3ebbbcd 2026-06-30 fix: restore 'Chi tiết thiết bị' card (detail dropdown + builder + text)
37f1d98 2026-06-30 feat(hmi): add billboard/MCB ON/OFF control from LVGL via bms/billboard/+/set
e75920e 2026-06-30 fix(l4): MCB schedule uses old ZbSend format (device+send+Cluster)
9579fb8 2026-06-30 fix(hmi+l5): MCB uses old ZbSend format (device+send+Cluster), AC keeps new format
571736f 2026-07-03 feat(hmi): AC event-driven feedback to LVGL via bms/ac/+/+ (debounce 200ms, anti-loop)
ed2cf68 2026-07-03 fix(hmi): match LVGL sign topic bms/sign/+/power/set for MCB control
af34a60 2026-07-03 fix(hmi): sync feedback keys to match DB - fan_speed + ambient_temp
d765e34 2026-07-03 fix(hmi): MCB ZbSend add Endpoint:1 to fix 'Missing endpoint' error
84c5fa6 2026-07-03 fix(hmi): MCB use NEW ZbSend format like AC (Device+Write)
8368747 2026-07-03 fix(hmi): MCB Write without EF00/ prefix - use raw attr ID like docs
aff1b33 2026-07-03 fix(hmi): MCB ZbSend add Endpoint:1 + EF00/ prefix (confirmed working)
6cd218c 2026-07-03 feat(hmi): add MCB sign feedback to LVGL via bms/sign/+/power
857fac3 2026-07-03 fix(hmi): use node.send() for feedback + add debug node
b86c7fb 2026-07-03 debug(hmi): add trace warns + raw debug node + fix delay type
c70ea7e 2026-07-03 fix(hmi): MCB state key is 'control' not 'power' (matches DB attrs mapping)
a71679b 2026-07-03 feat(hmi): rate-limit queue 20msg/s no-drop for burst MCB feedback
5b211ea 2026-07-03 feat(hmi): queue for MQTT in (AC+Sign) and MQTT out (feedback) - rate 20/s no-drop
c2db30d 2026-07-03 feat(queue): add rate-limit queues to all Tasmota MQTT paths (L1 in, HMI/L4/L5 out) - 20-50/s no-drop
563f798 2026-07-03 fix(queue): wire queue outputs to mqtt-out nodes (broken in prev commit)
782ac46 2026-07-03 fix(hmi): return msgs not [msgs] to send individual messages to mqtt-out
404f8ea 2026-07-03 debug(hmi): use node.send() + bypass queue-feedback to test
703a7b8 2026-07-03 debug(hmi): heavy trace logging in feedback function
0eda802 2026-07-03 fix(hmi): wire lk-hmi-feedback-in directly to fn-hmi-ac-feedback (parallel with debug)
604f237 2026-07-03 cleanup(hmi): remove debug nodes, clean feedback function, restore queue wiring
b16eaf7 2026-07-03 feat(dashboard): replace table with LVGL-style card grid (2-col responsive, expand, scenes)
2cd5f24 2026-07-03 feat(dashboard): native widgets ui-switch/ui-button/ui-text for device control + scenes
8edbbf4 2026-07-03 fix(dashboard): correct wiring fn-l5-cmd-builder -> queue-l5-cmd
4f87046 2026-07-03 feat(dashboard): 2-way sync for switches - update state from Tasmota feedback
80eac93 2026-07-03 fix(dashboard): ui-switch topicType msg->str to fix empty topic
1a89573 2026-07-03 feat(dashboard): per-device status text + removed dropdown/manual selection
14cf35a 2026-07-03 fix(dashboard): split sync into 3 separate functions for ui-text
639fdc4 2026-07-03 fix(dashboard): wire inject trigger to sync functions
84f38d4 2026-07-03 fix(dashboard): wire L2 link-in to sync functions + update switch states from Tasmota
7bb18c4 2026-07-04 feat(remote-bridge): add L6 Remote Bridge to mqtt.xsolar.energy
719c331 2026-07-04 fix(flows): correct L1 wiring and remote bridge key consistency
ca49975 2026-07-04 feat(remote-bridge): add current_voltage (meas_0275) + update integration doc
b6bede3 2026-07-05 docs(mqtt): rewrite integration guide for single-topic snapshot
93cf795 2026-07-05 feat(remote-bridge): event-driven sends only changed device
baf2cbe 2026-07-08 update readme tasmota flash for ZB-GW03
79676fe 2026-07-09 feat(remote-bridge): add safe-import-patch.json for zero-risk deploy
4d10fb5 2026-07-10 refactor: split code modules into separate repos
30cb634 2026-07-10 fix: add lvgl submodule to .gitmodules
f1bc5bd 2026-07-10 chore: update lvgl_project submodule (fix nested lvgl)
784943d 2026-07-10 docs: add multi-repo git workflow rules to AGENTS.md
a94f63e 2026-07-10 fix: remove broken ref_example submodule link
9548a6f 2026-07-10 chore: update lvgl_project submodule
6d3e410 2026-07-10 chore: remove draft flows.json (moved to Node-red submodule)
6f2e8e5 2026-07-10 chore: update Node-red submodule pointer
b485a36 2026-07-11 chore: update Node-red submodule pointer
c715aba 2026-07-11 chore: update Node-red submodule pointer
97965a9 2026-07-11 chore: update Node-red submodule pointer
1ea2586 2026-07-11 chore: update Node-red submodule pointer
8eaa78c 2026-07-11 chore: update Node-red submodule pointer
fff5fcc 2026-07-12 chore: update Node-red submodule pointer
1d72bc7 2026-07-12 chore: update Node-red submodule pointer
1084780 2026-07-14 docs: convert .docx to .md format for better version control
5e8418e 2026-07-14 chore: update lvgl_project submodule pointer
f4ec090 2026-07-25 feat: 6 device types + unified YAML config
f64384d 2026-07-25 chore: update submodules - unified devices.yaml + 6 device types
045518e 2026-07-25 docs: add Luckfox Pico setup and configuration guides
d89f2ab 2026-07-25 chore: update docs, add workspace, remove old firmware
05debc7 2026-07-25 docs: convert .docx to .md format for better git tracking
ccbfe60 2026-07-25 feat: add Metering (0x0702) and Diagnostics (0x0B05) attributes for SPM02-D2TZ power meters
2ba3849 2026-07-25 chore: update lvgl_project submodule - add Metering attributes for SPM02-D2TZ
80f6cfc 2026-07-25 feat: add dedicated card functions for power_meter and light_sensor
3b2589b 2026-07-25 chore: update lvgl_project submodule - dedicated cards for power_meter/light_sensor
f8f2226 2026-07-25 perf: optimize UI performance with 4-phase improvements
7bf318d 2026-07-25 perf: update lvgl_project submodule - UI performance optimizations
2d2f857 2026-07-25 feat: use English labels in UI, convert devices.yaml to English
2f76b96 2026-07-25 chore: update lvgl_project submodule - English labels in UI
cebd0f3 2026-07-25 feat: display units for attribute values in UI
fb92b28 2026-07-25 chore: update lvgl_project submodule - display units in UI
f2d373f 2026-07-25 feat: prioritize InstantaneousDemand over TotalActivePower
54fc2bd 2026-07-26 chore: update lvgl_project submodule - prioritize Instant Power (direct meter reading)
c5873e8 2026-07-26 feat: hide ON/OFF badge on sensor cards, hide Energy Received, show RMSVoltage
5e98e7a 2026-07-26 chore: update lvgl_project submodule
6b3c128 2026-07-26 feat: add overview field to control per-attr visibility on Overview vs expanded pages
7b1e183 2026-07-26 chore: update lvgl_project submodule
6132e13 2026-07-26 feat: 3-phase layout for power meter card (Row1: TE+Power, Row2: A/B/C columns)
b9fb94d 2026-07-26 chore: update lvgl_project submodule
73bfd1e 2026-07-26 fix: increase MAX_DISPLAY_ATTRS 8->16 to accommodate 3-phase power meter attrs
d945cd8 2026-07-26 chore: update lvgl_project submodule
7948b2e 2026-07-28 feat: implement Python BMS Engine to replace Node-RED
4b69ddb 2026-07-28 docs: update README.md with HMI Bridge, Xsolar Bridge, and architecture details
4da3ce6 2026-07-28 chore: remove Node-RED submodule
0c0ba30 2026-07-28 docs: add OTA update plan for Python BMS Engine
351c125 2026-07-28 feat: implement OTA update system
f7d4b0b 2026-07-28 docs: organize documentation into PlanAndDoc directory
4e7cbe8 2026-07-28 docs: update Setup documentation for Python BMS Engine migration
8c0cfe8 2026-07-28 docs: update installation guide to use USB/SSH/SFTP instead of git clone
2bc7425 2026-07-28 update: bump app version in ota check.json
7ca41e6 2026-07-28 docs: update OTA server setup for both LVGL App and Python BMS Engine
4adce6a 2026-07-28 feat: auto-reload devices.yaml without restart
e3ddb33 2026-07-30 fix: uncomment switch device in devices.yaml
8bbfc96 2026-07-30 chore: update lvgl_project submodule
586dfd4 2026-07-30 fix: increase switch card height (120->180), fix expanded switch layout (sr height 48px)
06c89ae 2026-07-30 chore: update lvgl_project submodule
89c3ed1 2026-07-31 feat: generic display_attr_control for YAML-driven UI controls
b3f9bf7 2026-07-31 chore: update lvgl_project submodule + add python engine queue manager
28391d7 2026-07-31 fix: bool feedback from Zigbee always ON (string '0' is truthy)
150ec69 2026-07-31 feat: zcl_illuminance formula for light sensor (lux = 10^((raw-1)/10000))
aecb4b8 2026-07-31 feat: zcl_illuminance formula for light sensor (lux = 10^((raw-1)/10000))
fac8ca3 2026-07-31 chore: update lvgl_project submodule (zcl_illuminance formula)
9210751 2026-07-31 perf: remove screen rebuild from display attr updates (use incremental label only)\n\n- Display attr value changes now only update label text (no full rebuild)\n- Increase throttle interval 500ms -> 2000ms\n- Fixes CPU spike from power meter data floods (20 attrs x 2 meters)
0605862 2026-07-31 chore: update lvgl_project submodule
4e876dd 2026-07-31 fix: sign board bool feedback not updating d->enabled from Zigbee
41df11b 2026-07-31 chore: update lvgl_project submodule
12233df 2026-07-31 feat: incremental card style update (bms_refresh_card_style)
9e516e7 2026-07-31 chore: update lvgl_project submodule
024d96f 2026-07-31 fix: bool feedback needs rebuild (switch+badge update) + throttle 1000ms
bd74ea4 2026-07-31 chore: update lvgl_project submodule
91bd463 2026-07-31 fix: power meter card Total Energy/Power always 0
43679d6 2026-07-31 chore: update lvgl_project submodule
ba31d71 2026-07-31 fix: Total Energy shows --- kWh when device doesnt report CurrentSummationDelivered
ccd5804 2026-07-31 chore: update lvgl_project submodule
10d6042 2026-07-31 feat: read Metering cluster (0x0702) energy from power meter
3544a3f 2026-07-31 feat: hex string parse + energy scale for power meter
f2b979c 2026-07-31 chore: update lvgl_project submodule
e4f33f9 2026-07-31 fix: TotalActivePower display: true (was false - power meter Power=0)
fb8d63a 2026-07-31 chore: update lvgl_project submodule
5a44374 2026-07-31 fix: CurrentSummation scale 0.001 -> 0.01 (divisor=100 for this meter)
9ef6c51 2026-07-31 chore: update lvgl_project submodule
07aa842 2026-07-31 fix: second power meter zigbee_addr 0x4BAD -> 0x8DF5
56a777d 2026-07-31 chore: update lvgl_project submodule
e6e57f2 2026-07-31 fix: add explicit tele/# subscription (paho v1 # alone fails)
22134a2 2026-07-31 fix: restore display: true for Illuminance (lost when adding formula)
0895d98 2026-07-31 chore: update lvgl_project submodule
1f4abc9 2026-07-31 feat: xsolar MQTT auth (user/pass in config.yaml, MQTTClient username/password)
819417b 2026-07-31 feat: event-driven xsolar push on new Zigbee data
cb75019 2026-07-31 perf: optimize get_latest_state query (GROUP BY instead of correlated subquery)
a747580 2026-07-31 perf: use ROW_NUMBER() window function for get_latest_state (single scan)
c94520e 2026-07-31 perf: bare GROUP BY + MAX(ts) for get_latest_state (pure index scan, no temp table)
e8115aa 2026-07-31 fix: sign board xsolar keys (control, energy, power, temperature, voltage, current)
1649cbf 2026-07-31 chore: update lvgl_project submodule
08068d5 2026-07-31 refactor: xsolar push uses label as key (not xsolar_key)
27e2e2d 2026-07-31 fix: remove rebuild from sign bool handler (causes freeze on frequent sign updates)
97a9641 2026-07-31 chore: update lvgl_project submodule
53ca343 2026-07-31 fix: sign rebuild only on state change (not every identical feedback)
979379b 2026-07-31 chore: update lvgl_project submodule
d360edb 2026-07-31 perf: rebuild_active_screen instead of rebuild_all_screens on toggle
852b396 2026-07-31 chore: update lvgl_project submodule
887b788 2026-07-31 fix: sign bool rebuild without throttle (immediate on state change)
fa9c203 2026-07-31 chore: update lvgl_project submodule
6d30fa4 2026-07-31 fix: feedback only publishes display:true attrs (stops sign flood)
58728c5 2026-07-31 feat: incremental switch update (bms_refresh_switch, no rebuild)
18bb137 2026-07-31 chore: update lvgl_project submodule
48ff496 2026-07-31 fix: always rebuild screen on navigate (sync overview + expanded state)
c5c0580 2026-07-31 chore: update lvgl_project submodule
8133a72 2026-07-31 perf: skip MQTT publish when Zigbee values unchanged (change tracking via _last_states)
6847441 2026-07-31 fix: online status (d->online set true on data + gateway LWT handler)
5835f27 2026-07-31 feat: gateway LWT handling (publish online/offline for all devices)
305a6a4 2026-07-31 feat: device offline detection via timeout (+ configurable offline_timeout in config.yaml)
e312102 2026-07-31 fix: per-device-type offline timeout (light_sensor=30min, AC=10min)
3e3e39a 2026-07-31 fix: skip offline check for never-seen devices (last_seen=0)
316f595 2026-07-31 perf: batch SQLite inserts (22 commits -> 1 transaction per Zigbee msg)
8e7da43 2026-07-31 feat: xsolar remote control for all device types (not just AC)
6790753 2026-08-01 docs: update xsolar integration guide to v4.0 (Python engine, label keys, 6 devices)
b0fa07a 2026-08-01 docs: fix xsolar guide - power meter only 7 attrs (not 23)
7c4128e 2026-08-01 update doc
cc056c5 2026-08-01 update wifi setup doc
70c6d15 2026-08-01 update doc index
55270ba 2026-08-01 update doc 0
a736bc2 2026-08-01 update doc 3_
7679271 2026-08-01 update doc
73bb4db 2026-08-01 update do4_
978e63f 2026-08-02 docs: hoàn thiện hướng dẫn touch (6 root causes + fix_touch.sh), tách doc NanoMQ, thêm doc cài app python
01e99c8 2026-08-02 update do7_
0a0fd50 2026-08-02 update do8_
1006f31 2026-08-02 fix(Rush_engine): build musl static, fix MQTT auth & xsolar client_id collision
95dc14a 2026-08-07 fix: parse device type from id (attrs 'type' collided) -> sign template correct
4147588 2026-08-07 chore: update lvgl_project submodule (sign template type fix)
5149da1 2026-08-07 fix: align legacy bms/ac MQTT control handler -> immediate rebuild like sign
8e17700 2026-08-07 chore: update lvgl_project submodule (align AC MQTT handler to immediate rebuild)
40ac30d 2026-08-07 fix: online dot updates live (rebuild on SSE data + treat data as online)
9b388a1 2026-08-07 chore: update lvgl_project submodule (online dot live update fix)
46a8e18 2026-08-07 fix: don't mark device online on arbitrary SSE attr (engine owns online status)
551255b 2026-08-07 chore: update lvgl_project submodule (online owned by engine, AC stays offline)
0340723 2026-08-07 fix: offline toggle keeps state + 'chua duoc xac nhan' banner; update zigbee addresses/gateway
3db0c3a 2026-08-07 feat(engine): replace python_engine with Rust HTTP API + fixes
b3f2760 2026-08-07 fix: keep set-attribute (+/-, fan, mode) state when device offline (no rollback to 0)
a1236bd 2026-08-07 chore: update lvgl_project submodule (keep set-attr state when offline)
febfe28 2026-08-07 fix(xsolar): use topic_prefix & site from config.yaml instead of hardcoded bluCafe
bb6fa47 2026-08-07 docs: align architecture/api/setup docs with current state
18616d2 2026-08-07 feat(engine): hot-reload rules.yaml via mtime check on 5s tick
756fafc 2026-08-07 feat(engine): implement real state/numeric_state conditions with value_path
406ea8d 2026-08-07 new rules engine
008d1ad 2026-08-07 chore: remove lvgl_project submodule to convert into subtree
1ac799b 2026-08-07 Add 'Device/Luckfoxpico86/code/lvgl_project/' from commit 'b3f2760deda573a3bf0d8512f0ab92999117f003'
f91fadd 2026-08-08 fix: remove orphaned nested submodule lvgl reference
428c277 2026-08-08 refactor repo
5a216e6 2026-08-08 feat(ota): GitHub Releases-based OTA cho cả 2 app
```
