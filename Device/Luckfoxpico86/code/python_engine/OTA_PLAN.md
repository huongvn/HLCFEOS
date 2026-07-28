# OTA Update Plan cho Python BMS Engine

## Tổng quan

Kế hoạch triển khai OTA (Over-The-Air) update cho Python BMS Engine, tương tự như OTA trong lvgl_project nhưng adapted cho Python.

## So sánh với OTA hiện tại (lvgl_project)

| Aspect | lvgl_project (C++) | python_engine (Python) |
|--------|-------------------|------------------------|
| Binary type | Executable binary | Python source files |
| Package format | Single file `app_v1.2.3` | Tarball/zip `bms_v1.2.3.tar.gz` |
| Update method | Atomic rename | Extract + symlink swap |
| Restart | System reboot | Service restart |
| Rollback | Boot flag | Backup directory |
| Health check | Boot success flag | Service status check |

## Kiến trúc OTA

```
┌─────────────────────────────────────────────────────────────┐
│                      OTA Server (Nginx)                      │
│  /ota/bms/check.json                                        │
│  /ota/bms/bms_v1.2.3.tar.gz                                 │
└─────────────────────────────────────────────────────────────┘
                            │
                            │ wget
                            ▼
┌─────────────────────────────────────────────────────────────┐
│                    Python BMS Engine                         │
│                                                              │
│  1. Check: Download check.json, compare version             │
│  2. Download: Download tarball to /tmp/bms_new.tar.gz       │
│  3. Verify: Check SHA256                                    │
│  4. Extract: Extract to /home/pico/bms_new/                 │
│  5. Backup: Backup current to /home/pico/bms_backup/        │
│  6. Swap: Atomic symlink swap                               │
│  7. Restart: systemctl restart bms-engine                   │
│  8. Health check: Verify service running                    │
│  9. Rollback: If fail, restore from backup                  │
└─────────────────────────────────────────────────────────────┘
```

## Chi tiết triển khai

### 1. OTA Server Setup

**File structure trên server:**
```
/var/www/ota_root/
└── bms/
    ├── check.json
    ├── bms_v1.0.0.tar.gz
    ├── bms_v1.0.1.tar.gz
    └── bms_v1.1.0.tar.gz
```

**check.json format:**
```json
{
  "version": "1.1.0",
  "filename": "bms_v1.1.0.tar.gz",
  "sha256": "a1b2c3d4e5f6...",
  "url": "http://192.168.1.171/ota/bms/bms_v1.1.0.tar.gz",
  "release_notes": "Bug fixes and performance improvements",
  "min_version": "1.0.0",
  "force_update": false
}
```

### 2. Package Structure

**Tarball contents:**
```
bms_v1.1.0.tar.gz
└── bms_v1.1.0/
    ├── src/
    │   ├── main.py
    │   ├── mqtt_client.py
    │   ├── state_manager.py
    │   ├── device_manager.py
    │   ├── rule_engine.py
    │   ├── scheduler.py
    │   ├── hmi_bridge.py
    │   └── xsolar_bridge.py
    ├── config/
    │   ├── config.yaml
    │   └── rules.yaml
    ├── requirements.txt
    └── VERSION
```

**VERSION file:**
```
1.1.0
```

### 3. OTA Module (ota.py)

```python
"""
OTA Update Module for Python BMS Engine
"""

import os
import json
import hashlib
import tarfile
import shutil
import subprocess
import logging
from pathlib import Path
from typing import Dict, Optional, Tuple
from enum import Enum

logger = logging.getLogger(__name__)


class OTAState(Enum):
    IDLE = "idle"
    CHECKING = "checking"
    AVAILABLE = "available"
    DOWNLOADING = "downloading"
    VERIFYING = "verifying"
    INSTALLING = "installing"
    SUCCESS = "success"
    FAILED = "failed"


class OTAUpdater:
    """OTA updater for Python BMS Engine"""
    
    def __init__(self, config: Dict):
        self.current_version = self._get_current_version()
        self.ota_url = config.get('ota_url', 'http://192.168.1.171/ota/bms/check.json')
        self.install_dir = Path(config.get('install_dir', '/home/pico/bms-engine'))
        self.temp_dir = Path('/tmp/bms_ota')
        self.backup_dir = Path('/home/pico/bms_backup')
        
        self.state = OTAState.IDLE
        self.progress = 0
        self.message = "System Up to Date"
        self.new_version = ""
        
        # Create temp directory
        self.temp_dir.mkdir(parents=True, exist_ok=True)
        
        logger.info(f"OTA updater initialized, current version: {self.current_version}")
    
    def _get_current_version(self) -> str:
        """Get current version from VERSION file"""
        version_file = Path(__file__).parent.parent / 'VERSION'
        if version_file.exists():
            return version_file.read_text().strip()
        return "0.0.0"
    
    def check_update(self) -> Tuple[bool, str]:
        """
        Check for available updates
        
        Returns:
            Tuple of (update_available, new_version)
        """
        self.state = OTAState.CHECKING
        self.message = "Checking for updates..."
        
        try:
            # Download check.json
            check_file = self.temp_dir / 'check.json'
            result = subprocess.run(
                ['wget', '-q', '-O', str(check_file), self.ota_url],
                capture_output=True,
                timeout=30
            )
            
            if result.returncode != 0:
                raise Exception(f"Failed to download check.json: {result.stderr}")
            
            # Parse check.json
            with open(check_file, 'r') as f:
                manifest = json.load(f)
            
            new_version = manifest.get('version', '')
            
            # Compare versions
            if new_version != self.current_version:
                self.state = OTAState.AVAILABLE
                self.new_version = new_version
                self.message = f"New version {new_version} available"
                logger.info(f"Update available: {self.current_version} -> {new_version}")
                return True, new_version
            else:
                self.state = OTAState.IDLE
                self.message = "System Up to Date"
                logger.info("System is up to date")
                return False, self.current_version
                
        except Exception as e:
            logger.error(f"Failed to check for updates: {e}")
            self.state = OTAState.FAILED
            self.message = f"Check failed: {str(e)}"
            return False, self.current_version
    
    def download_update(self, manifest: Dict) -> bool:
        """
        Download update package
        
        Args:
            manifest: Manifest dictionary from check.json
            
        Returns:
            True if download successful
        """
        if self.state != OTAState.AVAILABLE:
            return False
        
        self.state = OTAState.DOWNLOADING
        self.progress = 0
        self.message = "Downloading... 0%"
        
        try:
            url = manifest.get('url')
            filename = manifest.get('filename')
            download_path = self.temp_dir / filename
            
            # Download with progress
            result = subprocess.run(
                ['wget', '-O', str(download_path), url],
                capture_output=True,
                timeout=300  # 5 minutes timeout
            )
            
            if result.returncode != 0:
                raise Exception(f"Download failed: {result.stderr}")
            
            self.progress = 100
            self.message = "Download complete"
            logger.info(f"Downloaded {filename}")
            
            # Verify SHA256
            expected_sha256 = manifest.get('sha256')
            if expected_sha256:
                if not self._verify_sha256(download_path, expected_sha256):
                    raise Exception("SHA256 verification failed")
                logger.info("SHA256 verification passed")
            
            self.state = OTAState.VERIFYING
            return True
            
        except Exception as e:
            logger.error(f"Download failed: {e}")
            self.state = OTAState.FAILED
            self.message = f"Download failed: {str(e)}"
            return False
    
    def _verify_sha256(self, file_path: Path, expected_hash: str) -> bool:
        """Verify SHA256 hash of file"""
        sha256_hash = hashlib.sha256()
        with open(file_path, 'rb') as f:
            for byte_block in iter(lambda: f.read(4096), b""):
                sha256_hash.update(byte_block)
        
        actual_hash = sha256_hash.hexdigest()
        return actual_hash == expected_hash
    
    def install_update(self, manifest: Dict) -> bool:
        """
        Install update
        
        Args:
            manifest: Manifest dictionary from check.json
            
        Returns:
            True if installation successful
        """
        if self.state != OTAState.VERIFYING:
            return False
        
        self.state = OTAState.INSTALLING
        self.message = "Installing..."
        
        try:
            filename = manifest.get('filename')
            tarball_path = self.temp_dir / filename
            extract_dir = self.temp_dir / 'extracted'
            
            # Extract tarball
            logger.info(f"Extracting {filename}")
            with tarfile.open(tarball_path, 'r:gz') as tar:
                tar.extractall(extract_dir)
            
            # Find extracted directory
            extracted_dirs = list(extract_dir.iterdir())
            if len(extracted_dirs) != 1:
                raise Exception("Invalid tarball structure")
            
            new_version_dir = extracted_dirs[0]
            
            # Backup current version
            if self.install_dir.exists():
                logger.info("Creating backup...")
                if self.backup_dir.exists():
                    shutil.rmtree(self.backup_dir)
                shutil.copytree(self.install_dir, self.backup_dir)
                logger.info(f"Backup created at {self.backup_dir}")
            
            # Atomic swap using symlink
            temp_link = self.install_dir.parent / 'bms-engine-new'
            if temp_link.exists():
                temp_link.unlink()
            
            # Create symlink to new version
            os.symlink(new_version_dir, temp_link)
            
            # Atomic rename
            if self.install_dir.is_symlink():
                self.install_dir.unlink()
            elif self.install_dir.exists():
                # If it's a directory, rename it
                old_dir = self.install_dir.parent / 'bms-engine-old'
                if old_dir.exists():
                    shutil.rmtree(old_dir)
                self.install_dir.rename(old_dir)
            
            temp_link.rename(self.install_dir)
            
            logger.info("Installation complete")
            
            # Restart service
            logger.info("Restarting service...")
            result = subprocess.run(
                ['systemctl', 'restart', 'bms-engine'],
                capture_output=True,
                timeout=30
            )
            
            if result.returncode != 0:
                raise Exception(f"Service restart failed: {result.stderr}")
            
            # Wait for service to start
            import time
            time.sleep(5)
            
            # Health check
            if not self._health_check():
                raise Exception("Health check failed after restart")
            
            self.state = OTAState.SUCCESS
            self.message = f"Update successful! Now on version {self.new_version}"
            logger.info(f"Update successful: {self.current_version} -> {self.new_version}")
            
            # Update current version
            self.current_version = self.new_version
            
            # Cleanup
            self._cleanup()
            
            return True
            
        except Exception as e:
            logger.error(f"Installation failed: {e}")
            self.state = OTAState.FAILED
            self.message = f"Install failed: {str(e)}"
            
            # Rollback
            self._rollback()
            
            return False
    
    def _health_check(self) -> bool:
        """Check if service is running properly"""
        try:
            result = subprocess.run(
                ['systemctl', 'is-active', 'bms-engine'],
                capture_output=True,
                text=True,
                timeout=10
            )
            return result.stdout.strip() == 'active'
        except Exception as e:
            logger.error(f"Health check failed: {e}")
            return False
    
    def _rollback(self):
        """Rollback to previous version"""
        logger.warning("Rolling back to previous version...")
        
        try:
            if self.backup_dir.exists():
                # Remove failed installation
                if self.install_dir.exists():
                    if self.install_dir.is_symlink():
                        self.install_dir.unlink()
                    else:
                        shutil.rmtree(self.install_dir)
                
                # Restore backup
                shutil.copytree(self.backup_dir, self.install_dir)
                
                # Restart service
                subprocess.run(
                    ['systemctl', 'restart', 'bms-engine'],
                    capture_output=True,
                    timeout=30
                )
                
                logger.info("Rollback successful")
            else:
                logger.error("No backup available for rollback")
                
        except Exception as e:
            logger.error(f"Rollback failed: {e}")
    
    def _cleanup(self):
        """Clean up temporary files"""
        try:
            if self.temp_dir.exists():
                shutil.rmtree(self.temp_dir)
                self.temp_dir.mkdir(parents=True, exist_ok=True)
            logger.info("Cleanup complete")
        except Exception as e:
            logger.error(f"Cleanup failed: {e}")
    
    def get_status(self) -> Dict:
        """Get current OTA status"""
        return {
            'state': self.state.value,
            'progress': self.progress,
            'message': self.message,
            'current_version': self.current_version,
            'new_version': self.new_version
        }
```

### 4. Integration với main.py

```python
# In main.py __init__:
from ota import OTAUpdater

self.ota_updater = OTAUpdater(self.config)

# Add periodic task to check for updates
self.scheduler.add_periodic_task(
    3600,  # Check every hour
    self._check_ota_updates,
    name="Check OTA updates"
)

def _check_ota_updates(self):
    """Check for OTA updates"""
    update_available, new_version = self.ota_updater.check_update()
    if update_available:
        logger.info(f"OTA update available: {new_version}")
        # Auto-download and install (or notify user)
        # For now, we'll auto-install
        self._perform_ota_update()

def _perform_ota_update(self):
    """Perform OTA update"""
    try:
        # Download check.json again to get full manifest
        check_file = Path('/tmp/bms_ota/check.json')
        if not check_file.exists():
            logger.error("check.json not found")
            return
        
        import json
        with open(check_file, 'r') as f:
            manifest = json.load(f)
        
        # Download update
        if not self.ota_updater.download_update(manifest):
            logger.error("OTA download failed")
            return
        
        # Install update
        if not self.ota_updater.install_update(manifest):
            logger.error("OTA install failed")
            return
        
        logger.info("OTA update completed successfully")
        
    except Exception as e:
        logger.error(f"OTA update failed: {e}")
```

### 5. Build Script (build_ota.py)

```python
#!/usr/bin/env python3
"""
Build OTA package for Python BMS Engine
"""

import os
import sys
import tarfile
import hashlib
import json
from pathlib import Path

def calculate_sha256(file_path: Path) -> str:
    """Calculate SHA256 hash of file"""
    sha256_hash = hashlib.sha256()
    with open(file_path, 'rb') as f:
        for byte_block in iter(lambda: f.read(4096), b""):
            sha256_hash.update(byte_block)
    return sha256_hash.hexdigest()

def build_ota_package(version: str, source_dir: Path, output_dir: Path):
    """Build OTA package"""
    
    # Create package directory
    package_name = f"bms_v{version}"
    package_dir = output_dir / package_name
    
    if package_dir.exists():
        import shutil
        shutil.rmtree(package_dir)
    
    # Copy source files
    import shutil
    shutil.copytree(source_dir, package_dir)
    
    # Create VERSION file
    version_file = package_dir / 'VERSION'
    version_file.write_text(version)
    
    # Create tarball
    tarball_name = f"{package_name}.tar.gz"
    tarball_path = output_dir / tarball_name
    
    with tarfile.open(tarball_path, 'w:gz') as tar:
        tar.add(package_dir, arcname=package_name)
    
    # Calculate SHA256
    sha256 = calculate_sha256(tarball_path)
    
    # Create check.json
    check_json = {
        'version': version,
        'filename': tarball_name,
        'sha256': sha256,
        'url': f'http://192.168.1.171/ota/bms/{tarball_name}',
        'release_notes': f'Version {version}',
        'min_version': '1.0.0',
        'force_update': False
    }
    
    check_json_path = output_dir / 'check.json'
    with open(check_json_path, 'w') as f:
        json.dump(check_json, f, indent=2)
    
    # Cleanup package directory
    shutil.rmtree(package_dir)
    
    print(f"OTA package built: {tarball_path}")
    print(f"SHA256: {sha256}")
    print(f"Manifest: {check_json_path}")

if __name__ == '__main__':
    if len(sys.argv) != 2:
        print("Usage: build_ota.py <version>")
        sys.exit(1)
    
    version = sys.argv[1]
    source_dir = Path(__file__).parent
    output_dir = Path('ota')
    output_dir.mkdir(exist_ok=True)
    
    build_ota_package(version, source_dir, output_dir)
```

### 6. Deploy Script (deploy_ota.sh)

```bash
#!/bin/bash
# Deploy OTA package to server

set -e

VERSION=$1
if [ -z "$VERSION" ]; then
    echo "Usage: deploy_ota.sh <version>"
    exit 1
fi

# Build package
python3 build_ota.py $VERSION

# Copy to OTA server
OTA_SERVER="user@192.168.1.171"
OTA_DIR="/var/www/ota_root/bms"

echo "Deploying to OTA server..."
scp ota/bms_v${VERSION}.tar.gz ${OTA_SERVER}:${OTA_DIR}/
scp ota/check.json ${OTA_SERVER}:${OTA_DIR}/

echo "OTA package deployed successfully!"
echo "Version: $VERSION"
echo "Server: $OTA_SERVER:$OTA_DIR"
```

## Quy trình cập nhật

### Developer workflow

1. **Phát triển và test:**
   ```bash
   cd /home/huongnv/projects/HLCFEOS/Device/Luckfoxpico86/code/python_engine
   # Edit files...
   python3 src/main.py  # Test locally
   ```

2. **Build OTA package:**
   ```bash
   python3 build_ota.py 1.1.0
   ```

3. **Deploy lên OTA server:**
   ```bash
   ./deploy_ota.sh 1.1.0
   ```

4. **Commit và push:**
   ```bash
   git add .
   git commit -m "feat: add new feature"
   git push
   ```

### Device workflow (automatic)

1. **Check updates:** Mỗi giờ tự động check
2. **Download:** Tự động download nếu có update
3. **Verify:** Check SHA256
4. **Backup:** Backup version hiện tại
5. **Install:** Extract và swap
6. **Restart:** Restart service
7. **Health check:** Verify service running
8. **Rollback:** Nếu fail, rollback về version cũ

## Testing

### Test OTA update

```bash
# 1. Build package cũ
python3 build_ota.py 1.0.0

# 2. Deploy lên server
./deploy_ota.sh 1.0.0

# 3. Install version cũ trên device
cd /home/pico/bms-engine
# Install v1.0.0...

# 4. Build package mới
python3 build_ota.py 1.1.0

# 5. Deploy lên server
./deploy_ota.sh 1.1.0

# 6. Chờ device tự động update (hoặc trigger manual)
sudo journalctl -u bms-engine -f | grep OTA
```

### Test rollback

```bash
# 1. Simulate failed update
# Edit main.py to cause error
echo "syntax error" >> src/main.py

# 2. Build và deploy
python3 build_ota.py 1.1.1
./deploy_ota.sh 1.1.1

# 3. Watch rollback
sudo journalctl -u bms-engine -f | grep -E "OTA|Rollback"
```

## Security considerations

1. **HTTPS:** Sử dụng HTTPS cho OTA server
2. **SHA256:** Verify integrity của package
3. **Signature:** (Future) Sign packages với GPG
4. **Access control:** Restrict OTA server access
5. **Rollback:** Always keep backup

## Future enhancements

1. **Delta updates:** Chỉ download changed files
2. **Staged rollout:** Rollout dần dần cho nhiều devices
3. **A/B partitions:** Dual partition cho zero-downtime updates
4. **Remote trigger:** Trigger update từ xsolar cloud
5. **Update history:** Log tất cả updates vào database

## Conclusion

OTA cho Python BMS Engine đơn giản hơn C++ vì:
- Không cần compile
- Có thể dùng tarball
- Service restart thay vì system reboot
- Dễ dàng rollback

Tuy nhiên cần:
- Backup mechanism
- Health check sau update
- Rollback nếu fail
- SHA256 verification
