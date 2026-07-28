"""
OTA Update Module for Python BMS Engine

Handles over-the-air updates with:
- Version checking
- Package download with SHA256 verification
- Atomic installation with backup
- Automatic rollback on failure
- Health check after restart
"""

import os
import json
import hashlib
import tarfile
import shutil
import subprocess
import logging
import time
from pathlib import Path
from typing import Dict, Optional, Tuple
from enum import Enum

logger = logging.getLogger(__name__)


class OTAState(Enum):
    """OTA update states"""
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
        """
        Initialize OTA updater
        
        Args:
            config: Configuration dictionary with OTA settings
        """
        self.current_version = self._get_current_version()
        self.ota_url = config.get('ota_url', 'http://192.168.1.171/ota/bms/check.json')
        self.install_dir = Path(config.get('install_dir', '/home/pico/bms-engine'))
        self.temp_dir = Path(config.get('temp_dir', '/tmp/bms_ota'))
        self.backup_dir = Path(config.get('backup_dir', '/home/pico/bms_backup'))
        self.auto_update = config.get('auto_update', False)
        
        self.state = OTAState.IDLE
        self.progress = 0
        self.message = "System Up to Date"
        self.new_version = ""
        self.manifest = None
        
        # Create temp directory
        self.temp_dir.mkdir(parents=True, exist_ok=True)
        
        logger.info(f"OTA updater initialized, current version: {self.current_version}")
    
    def _get_current_version(self) -> str:
        """
        Get current version from VERSION file
        
        Returns:
            Version string or "0.0.0" if not found
        """
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
        logger.info(f"Checking for updates at {self.ota_url}")
        
        try:
            # Download check.json
            check_file = self.temp_dir / 'check.json'
            result = subprocess.run(
                ['wget', '-q', '-O', str(check_file), self.ota_url],
                capture_output=True,
                timeout=30
            )
            
            if result.returncode != 0:
                raise Exception(f"Failed to download check.json: {result.stderr.decode()}")
            
            # Parse check.json
            with open(check_file, 'r') as f:
                self.manifest = json.load(f)
            
            new_version = self.manifest.get('version', '')
            
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
    
    def download_update(self) -> bool:
        """
        Download update package
        
        Returns:
            True if download successful
        """
        if self.state != OTAState.AVAILABLE or not self.manifest:
            logger.error("Cannot download: not in AVAILABLE state or no manifest")
            return False
        
        self.state = OTAState.DOWNLOADING
        self.progress = 0
        self.message = "Downloading... 0%"
        
        try:
            url = self.manifest.get('url')
            filename = self.manifest.get('filename')
            download_path = self.temp_dir / filename
            
            logger.info(f"Downloading {filename} from {url}")
            
            # Download with progress tracking
            result = subprocess.run(
                ['wget', '-O', str(download_path), url],
                capture_output=True,
                timeout=300  # 5 minutes timeout
            )
            
            if result.returncode != 0:
                raise Exception(f"Download failed: {result.stderr.decode()}")
            
            self.progress = 100
            self.message = "Download complete"
            logger.info(f"Downloaded {filename} ({download_path.stat().st_size} bytes)")
            
            # Verify SHA256
            expected_sha256 = self.manifest.get('sha256')
            if expected_sha256:
                logger.info("Verifying SHA256...")
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
        """
        Verify SHA256 hash of file
        
        Args:
            file_path: Path to file
            expected_hash: Expected SHA256 hash
            
        Returns:
            True if hash matches
        """
        sha256_hash = hashlib.sha256()
        with open(file_path, 'rb') as f:
            for byte_block in iter(lambda: f.read(4096), b""):
                sha256_hash.update(byte_block)
        
        actual_hash = sha256_hash.hexdigest()
        logger.debug(f"Expected SHA256: {expected_hash}")
        logger.debug(f"Actual SHA256: {actual_hash}")
        return actual_hash == expected_hash
    
    def install_update(self) -> bool:
        """
        Install update
        
        Returns:
            True if installation successful
        """
        if self.state != OTAState.VERIFYING or not self.manifest:
            logger.error("Cannot install: not in VERIFYING state or no manifest")
            return False
        
        self.state = OTAState.INSTALLING
        self.message = "Installing..."
        
        try:
            filename = self.manifest.get('filename')
            tarball_path = self.temp_dir / filename
            extract_dir = self.temp_dir / 'extracted'
            
            # Clean extract directory
            if extract_dir.exists():
                shutil.rmtree(extract_dir)
            extract_dir.mkdir(parents=True)
            
            # Extract tarball
            logger.info(f"Extracting {filename}")
            with tarfile.open(tarball_path, 'r:gz') as tar:
                tar.extractall(extract_dir)
            
            # Find extracted directory
            extracted_dirs = list(extract_dir.iterdir())
            if len(extracted_dirs) != 1:
                raise Exception("Invalid tarball structure: expected single directory")
            
            new_version_dir = extracted_dirs[0]
            logger.info(f"Extracted to {new_version_dir}")
            
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
                if temp_link.is_symlink():
                    temp_link.unlink()
                else:
                    shutil.rmtree(temp_link)
            
            # Create symlink to new version
            os.symlink(new_version_dir, temp_link)
            logger.info(f"Created temporary symlink: {temp_link}")
            
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
            logger.info(f"Atomic swap complete: {self.install_dir}")
            
            # Restart service
            logger.info("Restarting service...")
            result = subprocess.run(
                ['systemctl', 'restart', 'bms-engine'],
                capture_output=True,
                timeout=30
            )
            
            if result.returncode != 0:
                raise Exception(f"Service restart failed: {result.stderr.decode()}")
            
            # Wait for service to start
            logger.info("Waiting for service to start...")
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
        """
        Check if service is running properly
        
        Returns:
            True if service is active
        """
        try:
            result = subprocess.run(
                ['systemctl', 'is-active', 'bms-engine'],
                capture_output=True,
                text=True,
                timeout=10
            )
            is_active = result.stdout.strip() == 'active'
            logger.info(f"Health check: service is {'active' if is_active else 'inactive'}")
            return is_active
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
                logger.info(f"Restored backup from {self.backup_dir}")
                
                # Restart service
                result = subprocess.run(
                    ['systemctl', 'restart', 'bms-engine'],
                    capture_output=True,
                    timeout=30
                )
                
                if result.returncode == 0:
                    logger.info("Rollback successful, service restarted")
                else:
                    logger.error(f"Rollback restart failed: {result.stderr.decode()}")
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
        """
        Get current OTA status
        
        Returns:
            Dictionary with status information
        """
        return {
            'state': self.state.value,
            'progress': self.progress,
            'message': self.message,
            'current_version': self.current_version,
            'new_version': self.new_version
        }
    
    def perform_update(self) -> bool:
        """
        Perform complete update process (check + download + install)
        
        Returns:
            True if update successful
        """
        logger.info("Starting complete update process")
        
        # Check for updates
        update_available, new_version = self.check_update()
        if not update_available:
            logger.info("No update available")
            return False
        
        # Download update
        if not self.download_update():
            logger.error("Download failed")
            return False
        
        # Install update
        if not self.install_update():
            logger.error("Installation failed")
            return False
        
        logger.info("Update process completed successfully")
        return True
