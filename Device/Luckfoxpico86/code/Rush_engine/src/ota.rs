use flate2::read::GzDecoder;
use log::{debug, error, info, warn};
use serde::{Deserialize, Serialize};
use sha2::{Digest, Sha256};
use std::fs;
use std::io::Read;
use std::path::{Path, PathBuf};
use std::process::Command;
use tar::Archive;

#[derive(Debug, Clone, PartialEq)]
pub enum OtaState {
    Idle,
    Checking,
    Available,
    Downloading,
    Verifying,
    Installing,
    Success,
    Failed,
}

impl OtaState {
    pub fn to_str(&self) -> &'static str {
        match self {
            OtaState::Idle => "idle",
            OtaState::Checking => "checking",
            OtaState::Available => "available",
            OtaState::Downloading => "downloading",
            OtaState::Verifying => "verifying",
            OtaState::Installing => "installing",
            OtaState::Success => "success",
            OtaState::Failed => "failed",
        }
    }
}

#[derive(Debug, Deserialize)]
pub struct OtaManifest {
    pub version: String,
    pub filename: String,
    pub sha256: String,
    pub url: String,
    #[serde(default)]
    pub release_notes: String,
    #[serde(default)]
    pub min_version: String,
    #[serde(default)]
    pub force_update: bool,
}

#[derive(Debug, Serialize)]
pub struct OtaStatus {
    pub state: String,
    pub progress: u8,
    pub message: String,
    pub current_version: String,
    pub new_version: String,
}

pub struct OtaUpdater {
    current_version: String,
    ota_url: String,
    install_dir: PathBuf,
    temp_dir: PathBuf,
    backup_dir: PathBuf,
    auto_update: bool,
    state: OtaState,
    progress: u8,
    message: String,
    new_version: String,
    manifest: Option<OtaManifest>,
}

impl OtaUpdater {
    pub fn new(
        current_version: String,
        ota_url: String,
        install_dir: PathBuf,
        temp_dir: PathBuf,
        backup_dir: PathBuf,
        auto_update: bool,
    ) -> Self {
        let _ = fs::create_dir_all(&temp_dir);

        info!(
            "OTA updater initialized, current version: {}",
            current_version
        );

        Self {
            current_version,
            ota_url,
            install_dir,
            temp_dir,
            backup_dir,
            auto_update,
            state: OtaState::Idle,
            progress: 0,
            message: "System Up to Date".to_string(),
            new_version: String::new(),
            manifest: None,
        }
    }

    pub fn current_version(&self) -> &str {
        &self.current_version
    }

    pub fn state(&self) -> &OtaState {
        &self.state
    }

    pub fn check_update(&mut self) -> Result<(bool, String), anyhow::Error> {
        self.state = OtaState::Checking;
        self.message = "Checking for updates...".to_string();
        info!("Checking for updates at {}", self.ota_url);

        let check_file = self.temp_dir.join("check.json");

        let output = Command::new("wget")
            .args(["-q", "-O", check_file.to_str().unwrap(), &self.ota_url])
            .output()?;

        if !output.status.success() {
            return Err(anyhow::anyhow!(
                "Failed to download check.json: {}",
                String::from_utf8_lossy(&output.stderr)
            ));
        }

        let content = fs::read_to_string(&check_file)?;
        let manifest: OtaManifest = serde_json::from_str(&content)?;

        let new_version = manifest.version.clone();

        if new_version != self.current_version {
            self.state = OtaState::Available;
            self.new_version = new_version.clone();
            self.message = format!("New version {} available", new_version);
            self.manifest = Some(manifest);
            info!(
                "Update available: {} -> {}",
                self.current_version, new_version
            );
            Ok((true, new_version))
        } else {
            self.state = OtaState::Idle;
            self.message = "System Up to Date".to_string();
            info!("System is up to date");
            Ok((false, self.current_version.clone()))
        }
    }

    pub fn download_update(&mut self) -> Result<bool, anyhow::Error> {
        if self.state != OtaState::Available || self.manifest.is_none() {
            error!("Cannot download: not in AVAILABLE state or no manifest");
            return Ok(false);
        }

        self.state = OtaState::Downloading;
        self.progress = 0;
        self.message = "Downloading... 0%".to_string();

        let manifest = self.manifest.as_ref().unwrap();
        let download_path = self.temp_dir.join(&manifest.filename);

        info!(
            "Downloading {} from {}",
            manifest.filename, manifest.url
        );

        let output = Command::new("wget")
            .args(["-O", download_path.to_str().unwrap(), &manifest.url])
            .output()?;

        if !output.status.success() {
            return Err(anyhow::anyhow!(
                "Download failed: {}",
                String::from_utf8_lossy(&output.stderr)
            ));
        }

        self.progress = 100;
        self.message = "Download complete".to_string();

        let size = fs::metadata(&download_path)?.len();
        info!("Downloaded {} ({} bytes)", manifest.filename, size);

        if !manifest.sha256.is_empty() {
            info!("Verifying SHA256...");
            if !verify_sha256(&download_path, &manifest.sha256)? {
                return Err(anyhow::anyhow!("SHA256 verification failed"));
            }
            info!("SHA256 verification passed");
        }

        self.state = OtaState::Verifying;
        Ok(true)
    }

    pub fn install_update(&mut self) -> Result<bool, anyhow::Error> {
        if self.state != OtaState::Verifying || self.manifest.is_none() {
            error!("Cannot install: not in VERIFYING state or no manifest");
            return Ok(false);
        }

        self.state = OtaState::Installing;
        self.message = "Installing...".to_string();

        let manifest = self.manifest.as_ref().unwrap();
        let tarball_path = self.temp_dir.join(&manifest.filename);
        let extract_dir = self.temp_dir.join("extracted");

        if extract_dir.exists() {
            fs::remove_dir_all(&extract_dir)?;
        }
        fs::create_dir_all(&extract_dir)?;

        info!("Extracting {}", manifest.filename);
        let file = fs::File::open(&tarball_path)?;
        let decoder = GzDecoder::new(file);
        let mut archive = Archive::new(decoder);
        archive.unpack(&extract_dir)?;

        let entries: Vec<PathBuf> = fs::read_dir(&extract_dir)?
            .filter_map(|e| e.ok().map(|e| e.path()))
            .collect();

        if entries.len() != 1 {
            return Err(anyhow::anyhow!(
                "Invalid tarball structure: expected single directory"
            ));
        }

        let new_version_dir = &entries[0];
        info!("Extracted to {:?}", new_version_dir);

        if self.install_dir.exists() {
            info!("Creating backup...");
            if self.backup_dir.exists() {
                fs::remove_dir_all(&self.backup_dir)?;
            }
            copy_dir_recursive(&self.install_dir, &self.backup_dir)?;
            info!("Backup created at {:?}", self.backup_dir);
        }

        let temp_link = self.install_dir.parent().unwrap().join("bms-engine-new");
        if temp_link.exists() {
            if temp_link.is_symlink() {
                fs::remove_file(&temp_link)?;
            } else {
                fs::remove_dir_all(&temp_link)?;
            }
        }

        std::os::unix::fs::symlink(new_version_dir, &temp_link)?;
        info!("Created temporary symlink: {:?}", temp_link);

        if self.install_dir.is_symlink() {
            fs::remove_file(&self.install_dir)?;
        } else if self.install_dir.exists() {
            let old_dir = self.install_dir.parent().unwrap().join("bms-engine-old");
            if old_dir.exists() {
                fs::remove_dir_all(&old_dir)?;
            }
            fs::rename(&self.install_dir, &old_dir)?;
        }

        fs::rename(&temp_link, &self.install_dir)?;
        info!("Atomic swap complete: {:?}", self.install_dir);

        info!("Restarting service...");
        let output = Command::new("systemctl")
            .args(["restart", "bms-engine"])
            .output()?;

        if !output.status.success() {
            return Err(anyhow::anyhow!(
                "Service restart failed: {}",
                String::from_utf8_lossy(&output.stderr)
            ));
        }

        info!("Waiting for service to start...");
        std::thread::sleep(std::time::Duration::from_secs(5));

        if !health_check()? {
            self.rollback()?;
            return Err(anyhow::anyhow!("Health check failed after restart"));
        }

        self.state = OtaState::Success;
        self.message = format!("Update successful! Now on version {}", self.new_version);
        info!(
            "Update successful: {} -> {}",
            self.current_version, self.new_version
        );

        self.current_version = self.new_version.clone();
        self.cleanup()?;

        Ok(true)
    }

    fn rollback(&self) -> Result<(), anyhow::Error> {
        warn!("Rolling back to previous version...");

        if self.backup_dir.exists() {
            if self.install_dir.exists() {
                if self.install_dir.is_symlink() {
                    fs::remove_file(&self.install_dir)?;
                } else {
                    fs::remove_dir_all(&self.install_dir)?;
                }
            }

            copy_dir_recursive(&self.backup_dir, &self.install_dir)?;
            info!("Restored backup from {:?}", self.backup_dir);

            let output = Command::new("systemctl")
                .args(["restart", "bms-engine"])
                .output()?;

            if output.status.success() {
                info!("Rollback successful, service restarted");
            } else {
                error!(
                    "Rollback restart failed: {}",
                    String::from_utf8_lossy(&output.stderr)
                );
            }
        } else {
            error!("No backup available for rollback");
        }

        Ok(())
    }

    fn cleanup(&self) -> Result<(), anyhow::Error> {
        if self.temp_dir.exists() {
            fs::remove_dir_all(&self.temp_dir)?;
            fs::create_dir_all(&self.temp_dir)?;
        }
        info!("Cleanup complete");
        Ok(())
    }

    pub fn get_status(&self) -> OtaStatus {
        OtaStatus {
            state: self.state.to_str().to_string(),
            progress: self.progress,
            message: self.message.clone(),
            current_version: self.current_version.clone(),
            new_version: self.new_version.clone(),
        }
    }

    pub fn perform_update(&mut self) -> Result<bool, anyhow::Error> {
        info!("Starting complete update process");

        let (update_available, _) = self.check_update()?;
        if !update_available {
            info!("No update available");
            return Ok(false);
        }

        if !self.download_update()? {
            error!("Download failed");
            return Ok(false);
        }

        if !self.install_update()? {
            error!("Installation failed");
            return Ok(false);
        }

        info!("Update process completed successfully");
        Ok(true)
    }
}

fn verify_sha256(file_path: &Path, expected_hash: &str) -> Result<bool, anyhow::Error> {
    let mut file = fs::File::open(file_path)?;
    let mut hasher = Sha256::new();
    let mut buffer = [0u8; 4096];

    loop {
        let bytes_read = file.read(&mut buffer)?;
        if bytes_read == 0 {
            break;
        }
        hasher.update(&buffer[..bytes_read]);
    }

    let actual_hash = format!("{:x}", hasher.finalize());
    debug!("Expected SHA256: {}", expected_hash);
    debug!("Actual SHA256: {}", actual_hash);
    Ok(actual_hash == expected_hash)
}

fn health_check() -> Result<bool, anyhow::Error> {
    let output = Command::new("systemctl")
        .args(["is-active", "bms-engine"])
        .output()?;

    let is_active = String::from_utf8_lossy(&output.stdout).trim() == "active";
    info!("Health check: service is {}", if is_active { "active" } else { "inactive" });
    Ok(is_active)
}

fn copy_dir_recursive(src: &Path, dst: &Path) -> Result<(), anyhow::Error> {
    if !dst.exists() {
        fs::create_dir_all(dst)?;
    }

    for entry in fs::read_dir(src)? {
        let entry = entry?;
        let path = entry.path();
        let dest = dst.join(entry.file_name());

        if path.is_dir() {
            copy_dir_recursive(&path, &dest)?;
        } else {
            fs::copy(&path, &dest)?;
        }
    }

    Ok(())
}
