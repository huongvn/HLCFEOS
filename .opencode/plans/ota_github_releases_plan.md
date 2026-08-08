# Plan: GitHub Releases-based OTA cho CẢ HAI App (C++ LVGL + Rust BMS Engine)

## 0. Decided final architecture

- **Monorepo**: cả 2 app cùng trong `huongvn/HLCFEOS` (submodule đã chuyển thành subtree).
- **2 workflow ở root** `.github/workflows/` (GitHub Actions chỉ đọc workflow ở root repo):
  - `release-app.yml` — trigger tag `app-v*` → build C++ LVGL, upload `app_v<VER>` + `.sha256`
  - `release-engine.yml` — trigger tag `bms-v*` → build Rust engine → upload `bms_v<VER>.tar.gz` + `.sha256`
- **Engine tarball**: chứa DUY NHẤT binary `bms-engine` (KHÔNG kèm config/rules).
- **C++ service**: `myapp.service` (root, sleep 30s, Restart=always) — đã tồn tại trên board.
- **No PAT**: cần token tùy chọn (ori_github_token) nếu vượt rate limit anon (60req/h).

## 1. Tóm tắt hiện trạng (Current State)

### 1.1. C++ LVGL App (`lvgl_project/`)
| Thành phần | Chi tiết |
|------------|----------|
| **Build** | `make APP_VERSION=1.1.0` → cross-compile `arm-linux-gnueabihf-gcc` static |
| **Output** | `ota/app_v<VER>` (binary), `ota/check.json` (manifest) |
| **Manifest (check.json)** | `{version, url, sha256}` — dùng cho OTA check |
| **OTA URL hiện tại** | `http://192.168.1.171/ota/check.json` (Nginx server nội bộ) |
| **OTA Client** | `src/ota.c` — dùng `wget` (system call) tải manifest + binary |
| **Install** | Atomic `rename(/home/pico/app_new, /home/pico/app)` + `chmod 755` + **reboot toàn bộ board** |
| **SHA256 verify** | Chưa implement (code comment `// In a real app, you would check SHA256 here`) |
| **Config** | `app_config.txt` (key=value), đọc qua `src/config.c` |
| **Service** | Chạy thủ công `/home/pico/app` — **chưa có systemd service** |

### 1.2. Rust BMS Engine (`Rush_engine/`)
| Thành phần | Chi tiết |
|------------|----------|
| **Build** | `./build.sh release` → `cargo zigbuild --release --target armv7-unknown-linux-musleabihf` |
| **Output** | `target/.../release/bms-engine` (static musl, ~3.4MB) |
| **OTA Package** | `./build_ota.sh <ver>` → tarball `bms_v<VER>.tar.gz` (binary + config/ + VERSION) + `check.json` |
| **Manifest (check.json)** | `{version, filename, sha256, url, release_notes, min_version, force_update}` |
| **OTA URL hiện tại** | `http://192.168.1.171/ota/bms/check.json` |
| **OTA Client** | `src/ota.rs` — `OtaUpdater` dùng `wget` (Command) tải manifest + tarball |
| **Install** | Extract tarball → symlink atomic swap (`/home/pico/bms-engine` → version dir) → `systemctl restart bms-engine` → health check (`systemctl is-active`) → rollback nếu fail |
| **SHA256 verify** | Đã implement đầy đủ (`verify_sha256`) |
| **Config** | `config.yaml` + `.env` (credentials) |
| **Service** | `bms-engine.service` (systemd, user=pico, auto-restart, depends on nanomq) |

### 1.3. OTA Server (Nginx nội bộ)
- Doc: `10_ota_server_setup.md`
- Server: Ubuntu 192.168.1.171, Nginx serve `/var/www/ota_root/`
- Structure: `/ota/check.json`, `/ota/app_v<VER>`, `/ota/bms/check.json`, `/ota/bms/bms_v<VER>.tar.gz`
- Deploy thủ công: `scp` binary/package + `check.json` lên server
- **Chưa có GitHub Actions / CI/CD**

### 1.4. Thư mục docs đã đọc
- `0. Cài đặt Ubuntu OS.md`
- `1_Thiet_lap_Luckfox_Pico_tu_dong_dang_nhap.md`
- `2_Cau_hinh_Wi-Fi_Access_Point_Luckfox.md`
- `3_huong_dan_cai_dat_mui_gio_luckfox.md`
- `4_Cau_hinh_luckfox_uart4_guide.md`
- `5_Cau_hinh_touch_cho_luckfox_pico_86.md`
- `6_Chay_app_Monitor_khi_Khoi_Dong.md` (chạy LVGL app tự động — dùng systemd? chưa thấy file service)
- `7_Cai_dat_NanoMQ.md`
- `9_Cai_app_Rust_BMS_Engine.md` (chi tiết build, deploy, config, service)
- `10_ota_server_setup.md` (Nginx OTA server cho cả 2 app)

---

## 2. Mục tiêu Plan: GitHub Releases-based OTA

Thống nhất OTA cho **cả 2 App** sử dụng **GitHub Releases** thay vì Nginx nội bộ:
- Mỗi release trên GitHub chứa artifact + `.sha256` sidecar
- Client (C++ và Rust) gọi GitHub API `GET /repos/{owner}/{repo}/releases/latest`
- Download artifact từ `browser_download_url` (CDN GitHub, HTTPS)
- Verify SHA256 → Atomic install → Restart/Reboot → Health check

---

## 3. Kiến trúc Target

```
GitHub Repositories
├── huongvn/HLCFEOS-lvgl_project      (submodule)  → C++ LVGL App releases
└── huongvn/HLCFEOS                   (root repo)  → Rust BMS Engine releases
                │
       GitHub Actions (ARM static build)
                │
       HTTPS API: /repos/{owner}/{repo}/releases/latest
                ▼
┌──────────────────────────────────────────────────────────────────┐
│ Luckfox Pico Panel 86                                            │
│  ┌─────────────────────┐     ┌─────────────────────────────────┐ │
│  │ C++ LVGL App        │     │ Rust BMS Engine (systemd)       │ │
│  │ /home/pico/app      │     │ /home/pico/bms-engine/bms-engine│ │
│  │ ota_check_now()     │     │ OtaUpdater::check_update()      │ │
│  │ ↓                   │     │ ↓                               │ │
│  │ GitHub API check    │     │ GitHub API check                │ │
│  │ → http_client HTTPS │     │ → reqwest HTTPS                 │ │
│  │ Download + verify   │     │ Download + verify               │ │
│  │ rename() atomic     │     │ → symlink swap + systemctl      │ │
│  │ reboot board        │     │ restart + health check          │ │
│  └─────────────────────┘     └─────────────────────────────────┘ │
└──────────────────────────────────────────────────────────────────┘
```

---

## 4. Chi tiết thay đổi theo từng App

### 4.1. C++ LVGL App (`lvgl_project/`)

#### Files tạo/sửa:
```
lvgl_project/
├── .github/workflows/release.yml           # NEW
├── src/config.c/h                          # MODIFY: github_repo, token, interval
├── src/ota.c/h                             # MODIFY (lớn): GitHub API + http_client
├── src/http_client.c/h                     # MODIFY: thêm HTTPS GET download streaming
├── Makefile                                # MODIFY: target release, gen .sha256
├── app_config.txt.example                  # NEW
└── doc/OTA_GITHUB_RELEASES.md              # NEW
```

#### Chi tiết implement:

**A. GitHub Actions (`.github/workflows/release.yml`)**
- Trigger: push tag `v*` hoặc `workflow_dispatch`
- Steps:
  1. Checkout với submodules
  2. Install `arm-linux-gnueabihf-gcc`, `make`, `file`
  3. Build: `make APP_VERSION=${VERSION#v}`
  4. Gen `.sha256`: `sha256sum ota/app_v${VER} > ota/app_v${VER}.sha256`
  5. Upload assets: `app_v${VER}` + `app_v${VER}.sha256`
  6. `generate_release_notes: true`

**B. Config mở rộng (`app_config.txt` + `config.c/h`)**
```ini
# GitHub OTA
github_repo=huongvn/HLCFEOS-lvgl_project
ota_github_token=ghp_xxxxxxxxxxxx   # optional, classic PAT scope: repo
ota_check_interval=86400            # 24h
ota_enabled=true

# Fallback giữ lại
ota_url=http://192.168.1.171/ota/check.json
```

**C. `http_client.c/h` mở rộng**
- Thêm function `http_download_file(url, dest_path, progress_cb)` dùng socket HTTPS (có thể dùng `curl` library hoặc `mbedtls` nhẹ — hoặc fallback `wget` nếu không muốn thêm dep)
- Hoặc đơn giản: dùng `http_request` có sẵn mở rộng để GET streaming download

**D. `ota.c` rewrite core logic**
- `ota_check_now()`: thay `wget` manifest bằng HTTPS GET GitHub API
  - URL: `https://api.github.com/repos/{owner}/{repo}/releases/latest`
  - Parse JSON: tìm `assets[]` có name `app_v*` + `app_v*.sha256`
  - So sánh `tag_name` (bỏ `v` prefix) với `APP_VERSION`
- `ota_start_download()`: download artifact + `.sha256` song song (hoặc tuần tự)
- `ota_update_progress()`: verify SHA256 ngay khi download xong (đọc file `.sha256`, so sánh)
- Giữ nguyên `ota_install_now()` (rename + chmod + reboot)

**E. Makefile**
- Thêm target `release`: build + gen `.sha256`
- Biến `GITHUB_REPO` default `huongvn/HLCFEOS-lvgl_project`

---

### 4.2. Rust BMS Engine (`Rush_engine/`)

#### Files tạo/sửa:
```
Rush_engine/
├── .github/workflows/release.yml           # NEW
├── Cargo.toml                              # MODIFY: deps reqwest + tokio-util
├── src/ota.rs                              # MODIFY (lớn): GitHub API + reqwest
├── src/main.rs                             # MODIFY: config github_repo, github_token
├── build_ota.sh                            # MODIFY: gen .sha256 cho release
├── deploy_ota.sh                           # DEPRECATE (thay bằng git tag push)
└── doc/OTA_GITHUB_RELEASES.md              # NEW
```

#### Chi tiết implement:

**A. GitHub Actions (`.github/workflows/release.yml`)**
- Trigger: push tag `v*` hoặc `workflow_dispatch`
- Steps:
  1. Checkout
  2. Install Rust + `armv7-unknown-linux-musleabihf` target + `cargo-zigbuild` + `zig`
  3. Build: `cargo zigbuild --release --target armv7-unknown-linux-musleabihf`
  4. Package OTA: copy binary + config/ + VERSION → tarball `bms_v${VER}.tar.gz`
  5. Gen `.sha256`: `sha256sum ota/bms_v${VER}.tar.gz > ota/bms_v${VER}.tar.gz.sha256`
  6. Upload assets: `bms_v${VER}.tar.gz` + `bms_v${VER}.tar.gz.sha256`
  6. `generate_release_notes: true`

**B. Cargo.toml deps mới**
```toml
reqwest = { version = "0.12", features = ["json", "rustls-tls", "stream"] }
tokio-util = "0.7"
```

**C. Config mở rộng (`config.yaml` + `main.rs`)**
```yaml
ota:
  enabled: true
  github_repo: "huongvn/HLCFEOS"       # root repo cho engine
  github_token: "ghp_xxxxxxxxxxxx"     # optional
  check_interval: 3600                 # 1h
  auto_update: false
  install_dir: "/home/pico/bms-engine"
  temp_dir: "/tmp/bms_ota"
  backup_dir: "/home/pico/bms-engine-backup"
  # Fallback
  ota_url: "http://192.168.1.171/ota/bms/check.json"
```

**D. `ota.rs` rewrite core logic**
- `check_update()`: thay `wget` check.json bằng `reqwest` GET GitHub API
  - `GET https://api.github.com/repos/{owner}/{repo}/releases/latest`
  - Parse assets: tìm `bms_v*.tar.gz` + `bms_v*.tar.gz.sha256`
  - So sánh version (semver parse)
- `download_update()`: streaming download artifact + `.sha256` dùng `reqwest` + `tokio::io::AsyncWriteExt`
  - Progress callback để update progress
- `install_update()`: giữ nguyên logic (extract → symlink swap → systemctl restart → health check)
- Thêm verify SHA256 từ asset `.sha256` (đã có `verify_sha256`)

**E. `build_ota.sh`**
- Sau khi tạo tarball, gen `.sha256` sidecar
- Deploy script `deploy_ota.sh` → DEPRECATE (chỉ giữ để backward compat)

---

## 5. GitHub Actions Workflow (Chi tiết)

### 5.1. LVGL App (`.github/workflows/release.yml`)
```yaml
name: Build & Release LVGL App
on:
  push:
    tags: ['v*']
  workflow_dispatch:
    inputs:
      version:
        description: 'Version (e.g., 1.2.3)'
        required: true

jobs:
  build:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
        with: { submodules: recursive }
      - name: Install cross-toolchain
        run: |
          sudo apt-get update
          sudo apt-get install -y gcc-arm-linux-gnueabihf make file
      - name: Build
        env:
          VERSION: ${{ github.ref_name == 'refs/heads/main' && inputs.version || github.ref_name }}
        run: |
          cd Device/Luckfoxpico86/code/lvgl_project
          VER=${VERSION#v}
          make APP_VERSION=$VER
      - name: Generate SHA256
        run: |
          cd Device/Luckfoxpico86/code/lvgl_project/ota
          sha256sum app_v${VER} > app_v${VER}.sha256
      - name: Create Release
        uses: softprops/action-gh-release@v2
        with:
          files: |
            Device/Luckfoxpico86/code/lvgl_project/ota/app_v${VER}
            Device/Luckfoxpico86/code/lvgl_project/ota/app_v${VER}.sha256
          generate_release_notes: true
```

### 5.2. Rust Engine (`.github/workflows/release.yml`)
```yaml
name: Build & Release BMS Engine
on:
  push:
    tags: ['v*']
  workflow_dispatch:
    inputs:
      version:
        description: 'Version (e.g., 1.2.0)'
        required: true

jobs:
  build:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: Install Rust + ARM target
        run: |
          rustup target add armv7-unknown-linux-musleabihf
          cargo install cargo-zigbuild
      - name: Install zig
        run: |
          wget -q https://ziglang.org/builds/zig-linux-x86_64-0.13.0.tar.xz
          tar xf zig-linux-x86_64-0.13.0.tar.xz
          echo "$PWD/zig-linux-x86_64-0.13.0" >> $GITHUB_PATH
      - name: Build release
        env:
          VERSION: ${{ github.ref_name == 'refs/heads/main' && inputs.version || github.ref_name }}
        run: |
          cd Device/Luckfoxpico86/code/Rush_engine
          VER=${VERSION#v}
          cargo zigbuild --release --target armv7-unknown-linux-musleabihf
      - name: Package OTA
        run: |
          cd Device/Luckfoxpico86/code/Rush_engine
          VER=${VERSION#v}
          mkdir -p ota/bms_v${VER}/config
          cp target/armv7-unknown-linux-musleabihf/release/bms-engine ota/bms_v${VER}/bms-engine
          cp config/config.yaml ota/bms_v${VER}/config/
          cp config/rules.yaml ota/bms_v${VER}/config/
          echo $VER > ota/bms_v${VER}/VERSION
          tar -czf ota/bms_v${VER}.tar.gz -C ota bms_v${VER}
          sha256sum ota/bms_v${VER}.tar.gz > ota/bms_v${VER}.tar.gz.sha256
      - name: Create Release
        uses: softprops/action-gh-release@v2
        with:
          files: |
            Device/Luckfoxpico86/code/Rush_engine/ota/bms_v${VER}.tar.gz
            Device/Luckfoxpico86/code/Rush_engine/ota/bms_v${VER}.tar.gz.sha256
          generate_release_notes: true
```

---

## 6. Quy trình Release (Developer)

### C++ LVGL App
```bash
cd Device/Luckfoxpico86/code/lvgl_project
make APP_VERSION=1.2.3          # builds ota/app_v1.2.3 + .sha256
git tag v1.2.3
git push origin v1.2.3          # → Actions build + Release
```

### Rust BMS Engine
```bash
cd Device/Luckfoxpico86/code/Rush_engine
cargo set-version 1.2.0         # hoặc edit Cargo.toml
git tag v1.2.0
git push origin v1.2.0          # → Actions build + Release
```

---

## 7. Cấu hình Board (Deploy)

### C++ App (`app_config.txt`)
```ini
github_repo=huongvn/HLCFEOS-lvgl_project
ota_github_token=ghp_xxx          # optional
ota_check_interval=86400          # 24h
ota_enabled=true
# Fallback
ota_url=http://192.168.1.171/ota/check.json
```

### Rust Engine (`config.yaml`)
```yaml
ota:
  enabled: true
  github_repo: "huongvn/HLCFEOS"
  github_token: "ghp_xxx"
  check_interval: 3600
  auto_update: false
  install_dir: "/home/pico/bms-engine"
  temp_dir: "/tmp/bms_ota"
  backup_dir: "/home/pico/bms-engine-backup"
  # Fallback
  ota_url: "http://192.168.1.171/ota/bms/check.json"
```

---

## 8. Rủi ro & Mitigation

| Rủi ro | Mitigation |
|--------|------------|
| Rate limit GitHub (60/h anon) | Cấu hình `ota_github_token` (PAT classic, scope `repo`) → 5000/h |
| Mạng không ra Internet | Fallback `ota_url` local (Nginx 192.168.1.171) — giữ nguyên logic hiện tại |
| Binary sai arch | CI check `file` output = ARM 32-bit static trước khi upload |
| SHA256 mismatch | Abort download, log error, stay current version |
| Reboot loop (C++) | Cần `bms-app.service` + health check post-reboot |
| Reboot loop (Rust) | Đã có health check `systemctl is-active bms-engine` + rollback auto |
| Pre-release/draft tag | Bỏ qua (`draft=false, prerelease=false`) |

---

## 9. Checklist Triển khai

| Phase | Task | App | Est. |
|-------|------|-----|------|
| 1 | GitHub Actions workflows (2 repo) | Cả 2 | 0.5d |
| 2.1 | Config fields + http_client HTTPS | C++ | 0.5d |
| 2.2 | `ota.c` GitHub API + download + verify | C++ | 1d |
| 2.3 | Test E2E C++ (tag → board → install) | C++ | 0.5d |
| 3.1 | Cargo deps (`reqwest` + `tokio-util`) + config | Rust | 0.5d |
| 3.2 | `ota.rs` reqwest GitHub API + streaming | Rust | 1.5d |
| 3.3 | Test E2E Rust | Rust | 0.5d |
| 4 | `bms-app.service` + docs | Cả 2 | 0.5d |
| **Tổng** | | | **~5.5 ngày** |

---

## 10. Cần xác nhận từ bạn

| # | Câu hỏi | Mặc định khuyến nghị |
|---|---------|---------------------|
| 1 | Repo C++ App: `huongvn/HLCFEOS-lvgl_project` (submodule) hay root `HLCFEOS`? | Submodule LVGL |
| 2 | Repo Rust Engine: `huongvn/HLCFEOS` (root) | Root repo |
| 3 | Có PAT `ghp_...` (classic, scope `repo`) cho 2 app? Không → anon 60req/h (đủ 24h/check) | Anon OK |
| 4 | C++ app chạy thủ công `/home/pico/app` — tạo `bms-app.service` cho health check post-reboot? | **Yes** |
| 5 | Giữ fallback `ota_url` local (192.168.1.171) khi GitHub fail? | **Yes** |
| 6 | Asset naming: C++ `app_v<VER>`, Engine `bms_v<VER>.tar.gz` | Giữ nguyên |
| 7 | Engine tarball chứa `bms-engine` + `config/` (giữ nguyên) hay chỉ binary? | Giữ nguyên |

---

## 11. Files sẽ tạo/sửa (Tóm tắt)

```
# C++ LVGL App
Device/Luckfoxpico86/code/lvgl_project/
├── .github/workflows/release.yml           # NEW
├── src/config.c/h                          # MODIFY
├── src/ota.c/h                             # MODIFY (lớn)
├── src/http_client.c/h                     # MODIFY
├── Makefile                                # MODIFY
├── app_config.txt.example                  # NEW
└── doc/OTA_GITHUB_RELEASES.md              # NEW

# Rust BMS Engine
Device/Luckfoxpico86/code/Rush_engine/
├── .github/workflows/release.yml           # NEW
├── Cargo.toml                              # MODIFY (deps reqwest)
├── src/ota.rs                              # MODIFY (lớn)
├── src/main.rs                             # MODIFY (config)
├── build_ota.sh                            # MODIFY (gen .sha256)
├── deploy_ota.sh                           # DEPRECATE
└── doc/OTA_GITHUB_RELEASES.md              # NEW
```

---

**Sẵn sàng triển khai khi bạn xác nhận 7 câu hỏi ở Mục 10.**