use flate2::read::GzDecoder;
use log::{debug, error, info, warn};
use reqwest::header::{HeaderMap, HeaderValue, ACCEPT, AUTHORIZATION, USER_AGENT};
use serde::{Deserialize, Serialize};
use sha2::{Digest, Sha256};
use std::fs as std_fs;
use std::io::Read;
use std::path::{Path, PathBuf};
use std::process::Command;
use std::time::Duration;
use tar::Archive;
use tokio::fs;
use tokio::io::AsyncWriteExt;
use tokio::time::sleep;

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
pub struct GitHubRelease {
    pub tag_name: String,
    pub assets: Vec<GitHubAsset>,
}

#[derive(Debug, Deserialize)]
pub struct GitHubAsset {
    pub name: String,
    pub browser_download_url: String,
    pub size: u64,
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
    ota_url: String, // Kept for backward compat (fallback)
    github_repo: String,
    github_token: String,
    install_dir: PathBuf,
    temp_dir: PathBuf,
    backup_dir: PathBuf,
    auto_update: bool,
    state: OtaState,
    progress: u8,
    message: String,
    new_version: String,
    manifest: Option<OtaManifest>,
    github_client: reqwest::Client,
}

impl OtaUpdater {
    pub fn new(
        current_version: String,
        ota_url: String, // Kept for backward compat (fallback)
        install_dir: PathBuf,
        temp_dir: PathBuf,
        backup_dir: PathBuf,
        auto_update: bool,
        github_repo: String,
        github_token: String,
    ) -> Self {
        let _ = std_fs::create_dir_all(&temp_dir);

        let github_client = {
            let mut headers = HeaderMap::new();
            headers.insert(ACCEPT, HeaderValue::from_static("application/vnd.github.v3+json"));
            headers.insert(USER_AGENT, HeaderValue::from_static("bms-engine/1.0"));
            
            let client_builder = reqwest::Client::builder()
                .default_headers(headers)
                .use_rustls_tls()
                .timeout(Duration::from_secs(60));

            client_builder.build().unwrap()
        };

        info!(
            "OTA updater initialized, current version: {}",
            current_version
        );
        info!("GitHub repo: {}", github_repo);

        Self {
            current_version,
            ota_url,
            github_repo,
            github_token,
            install_dir,
            temp_dir,
            backup_dir,
            auto_update,
            state: OtaState::Idle,
            progress: 0,
            message: "System Up to Date".to_string(),
            new_version: String::new(),
            manifest: None,
            github_client,
        }
    }

    pub fn current_version(&self) -> &str {
        &self.current_version
    }

    pub fn state(&self) -> &OtaState {
        &self.state
    }

    pub async fn check_update(&mut self) -> Result<(bool, String), anyhow::Error> {
        self.state = OtaState::Checking;
        self.message = "Checking GitHub Releases...".to_string();
        info!("Checking for updates from GitHub: {}", self.github_repo);

        // Try GitHub API first
        match self.check_github_release().await {
            Ok((available, version)) => {
                if available {
                    return Ok((true, version));
                }
            }
            Err(e) => {
                warn!("GitHub API check failed: {}, falling back to OTA URL", e);
            }
        }

        // Fallback to OTA URL (legacy check.json)
        self.state = OtaState::Checking;
        self.message = "Checking fallback OTA URL...".to_string();
        info!("Checking fallback at {}", self.ota_url);

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

        let content = std_fs::read_to_string(&check_file)?;
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

    async fn check_github_release(&mut self) -> Result<(bool, String), anyhow::Error> {
        let url = format!(
            "https://api.github.com/repos/{}/releases/latest",
            self.github_repo
        );

        let mut request = self.github_client.get(&url);
        
        if !self.github_token.is_empty() {
            let auth_val = format!("token {}", self.github_token);
            request = request.header(AUTHORIZATION, auth_val);
        }

        let response = request.send().await?;

        if !response.status().is_success() {
            return Err(anyhow::anyhow!(
                "GitHub API returned status: {}",
                response.status()
            ));
        }

        let release: GitHubRelease = response.json().await?;

        // Parse version from tag_name (strip 'v' prefix)
        let new_version = release.tag_name.trim_start_matches('v').to_string();

        if new_version == self.current_version {
            info!("System is up to date (GitHub)");
            self.state = OtaState::Idle;
            self.message = "System Up to Date".to_string();
            return Ok((false, self.current_version.clone()));
        }

        // Find tarball asset (bms_v*.tar.gz)
        let tarball_asset = release.assets.iter().find(|a| {
            a.name.starts_with("bms_v") && a.name.ends_with(".tar.gz")
        });

        let sha256_asset = release.assets.iter().find(|a| {
            a.name.starts_with("bms_v") && a.name.ends_with(".tar.gz.sha256")
        });

        let (Some(tarball), Some(sha256_asset)) = (tarball_asset, sha256_asset) else {
            return Err(anyhow::anyhow!("Required assets not found in release"));
        };

        // Download SHA256 first to get expected hash
        let sha256_url = &sha256_asset.browser_download_url;
        let sha256_response = self.github_client.get(sha256_url).send().await?;
        let sha256_content = sha256_response.text().await?;
        
        let expected_sha256 = sha256_content
            .split_whitespace()
            .next()
            .ok_or_else(|| anyhow::anyhow!("Invalid SHA256 file format"))?
            .to_string();

        info!("Expected SHA256: {}", expected_sha256);

        // Create manifest for download/install
        let manifest = OtaManifest {
            version: new_version.clone(),
            filename: tarball.name.clone(),
            sha256: expected_sha256,
            url: tarball.browser_download_url.clone(),
            release_notes: String::new(),
            min_version: String::new(),
            force_update: false,
        };

        self.state = OtaState::Available;
        self.new_version = new_version.clone();
        self.message = format!("New version {} available", new_version);
        self.manifest = Some(manifest);
        
        info!(
            "Update available: {} -> {}",
            self.current_version, new_version
        );
        Ok((true, new_version))
    }

    pub async fn download_update(&mut self) -> Result<bool, anyhow::Error> {
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

        let mut request = self.github_client.get(&manifest.url);
        
        if !self.github_token.is_empty() {
            request = request.header(AUTHORIZATION, format!("token {}", self.github_token));
        }

        let response = request.send().await?;

        if !response.status().is_success() {
            return Err(anyhow::anyhow!(
                "Download failed with status: {}",
                response.status()
            ));
        }

        let mut response = response;

let total_size = response.content_length().unwrap_or(0) as usize;
        let mut downloaded = 0;
        let mut file = fs::File::create(&download_path).await?;

        while let Some(chunk) = response.chunk().await? {
            file.write_all(&chunk).await?;
            downloaded += chunk.len();

            if total_size > 0 {
                self.progress = ((downloaded * 100) / total_size).min(100) as u8;
                self.message = format!("Downloading... {}%", self.progress);
            }
        }

        file.flush().await?;
        drop(file);

        self.progress = 100;
        self.message = "Download complete".to_string();

        let size = std_fs::metadata(&download_path)?.len();
        info!("Downloaded {} ({} bytes)", manifest.filename, size);

        // Verify SHA256
        info!("Verifying SHA256...");
        if !verify_sha256(&download_path, &manifest.sha256)? {
            return Err(anyhow::anyhow!("SHA256 verification failed"));
        }
        info!("SHA256 verification passed");

        self.state = OtaState::Verifying;
        Ok(true)
    }

    pub async fn install_update(&mut self) -> Result<bool, anyhow::Error> {
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
            std_fs::remove_dir_all(&extract_dir)?;
        }
        std_fs::create_dir_all(&extract_dir)?;

        info!("Extracting {}", manifest.filename);
        let file = std_fs::File::open(&tarball_path)?;
        let decoder = GzDecoder::new(file);
        let mut archive = Archive::new(decoder);
        archive.unpack(&extract_dir)?;

        // Binary-only tarball: must contain exactly the `bms-engine` executable.
        let new_binary = extract_dir.join("bms-engine");
        if !new_binary.is_file() {
            return Err(anyhow::anyhow!(
                "Invalid tarball structure: expected a single binary 'bms-engine'"
            ));
        }

        let install_binary = self.install_dir.join("bms-engine");

        // Backup current binary
        let backup_binary = self.backup_dir.join("bms-engine");
        if install_binary.exists() {
            info!("Creating backup...");
            if self.backup_dir.exists() {
                std_fs::remove_dir_all(&self.backup_dir)?;
            }
            std_fs::create_dir_all(&self.backup_dir)?;
            std_fs::copy(&install_binary, &backup_binary)?;
            info!("Backup created at {:?}", backup_binary);
        }

        // Atomically replace the binary (copy to temp then rename)
        let tmp_install = extract_dir.join("bms-engine.staged");
        std_fs::copy(&new_binary, &tmp_install)?;
        {
            use std::os::unix::fs::PermissionsExt;
            let mut perms = std_fs::metadata(&tmp_install)?.permissions();
            perms.set_mode(0o755);
            std_fs::set_permissions(&tmp_install, perms)?;
        }
        std_fs::rename(&tmp_install, &install_binary)?;
        info!("Installed new binary at {:?}", install_binary);

        // Restart service
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
        sleep(Duration::from_secs(5)).await;

        if !health_check().await? {
            self.rollback().await?;
            return Err(anyhow::anyhow!("Health check failed after restart"));
        }

        self.state = OtaState::Success;
        self.message = format!("Update successful! Now on version {}", self.new_version);
        info!(
            "Update successful: {} -> {}",
            self.current_version, self.new_version
        );

        self.current_version = self.new_version.clone();
        self.cleanup().await?;

        Ok(true)
    }

    async fn rollback(&self) -> Result<(), anyhow::Error> {
        warn!("Rolling back to previous version...");

        let backup_binary = self.backup_dir.join("bms-engine");
        if backup_binary.exists() {
            let install_binary = self.install_dir.join("bms-engine");
            std_fs::rename(&backup_binary, &install_binary)?;
            info!("Restored backup binary to {:?}", install_binary);

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

    async fn cleanup(&mut self) -> Result<(), anyhow::Error> {
        if self.temp_dir.exists() {
            std_fs::remove_dir_all(&self.temp_dir)?;
            std_fs::create_dir_all(&self.temp_dir)?;
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

    pub async fn perform_update(&mut self) -> Result<bool, anyhow::Error> {
        info!("Starting complete update process");

        let (update_available, _) = self.check_update().await?;
        if !update_available {
            info!("No update available");
            return Ok(false);
        }

        if !self.download_update().await? {
            error!("Download failed");
            return Ok(false);
        }

        if !self.install_update().await? {
            error!("Installation failed");
            return Ok(false);
        }

        info!("Update process completed successfully");
        Ok(true)
    }
}

fn verify_sha256(file_path: &Path, expected_hash: &str) -> Result<bool, anyhow::Error> {
    let mut file = std_fs::File::open(file_path)?;
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

async fn health_check() -> Result<bool, anyhow::Error> {
    let output = Command::new("systemctl")
        .args(["is-active", "bms-engine"])
        .output()?;

    let is_active = String::from_utf8_lossy(&output.stdout).trim() == "active";
    info!("Health check: service is {}", if is_active { "active" } else { "inactive" });
    Ok(is_active)
}