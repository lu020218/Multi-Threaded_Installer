//! Component manifest loading/downloading/verifying helpers.

use std::fs;
use std::io::{Read, Write};
use std::path::{Path, PathBuf};
use std::time::Duration;

use base64::engine::general_purpose::STANDARD as BASE64_STD;
use base64::Engine;
use ed25519_dalek::{Signature, Verifier, VerifyingKey};
use reqwest::blocking::Client;
use reqwest::Url;
use serde::{Deserialize, Serialize};
use sha2::{Digest, Sha256};

use installer_shared::{InstallerError, Result};

/// Optional component manifest.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct ComponentManifest {
    pub version: u32,
    #[serde(default)]
    pub channel: Option<String>,
    #[serde(default)]
    pub public_key_id: Option<String>,
    #[serde(default)]
    pub components: Vec<ComponentEntry>,
}

/// Single component definition.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct ComponentEntry {
    pub id: String,
    pub display_name: String,
    pub version: String,
    #[serde(default)]
    pub required: bool,
    pub package: ComponentPackage,
    #[serde(default)]
    pub install: Option<ComponentInstallSpec>,
    #[serde(default)]
    pub rollback: Option<ComponentRollbackSpec>,
}

/// Download package metadata.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct ComponentPackage {
    pub url: String,
    pub size: u64,
    pub sha256: String,
    #[serde(default)]
    pub signature: Option<String>,
}

/// Component installation specification.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct ComponentInstallSpec {
    /// install kind: archive/msi/exe/script
    pub kind: String,
    #[serde(default)]
    pub target_subdir: Option<String>,
    #[serde(default)]
    pub entrypoint: Option<String>,
    #[serde(default)]
    pub args: Option<String>,
}

/// Component rollback specification.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize, Default)]
pub struct ComponentRollbackSpec {
    #[serde(default)]
    pub remove_paths: Vec<String>,
    #[serde(default)]
    pub uninstall_product_code: Option<String>,
}

/// Download policy for component packages.
#[derive(Debug, Clone)]
pub struct ComponentDownloadPolicy {
    /// Total number of attempts (including first try).
    pub attempts: u32,
    /// Per-request timeout.
    pub timeout: Duration,
    /// Optional allowed host list for HTTP/HTTPS URLs.
    /// Empty means no host restriction.
    pub allowed_hosts: Vec<String>,
}

impl Default for ComponentDownloadPolicy {
    fn default() -> Self {
        Self {
            attempts: 3,
            timeout: Duration::from_secs(30),
            allowed_hosts: Vec::new(),
        }
    }
}

/// Signature verification policy for components.
#[derive(Debug, Clone, Default)]
pub struct ComponentSignaturePolicy {
    /// key_id -> Ed25519 public key.
    pub public_keys: std::collections::HashMap<String, VerifyingKey>,
}

impl ComponentSignaturePolicy {
    /// Build keyring from environment variable:
    /// `MTI_COMPONENT_PUBLIC_KEYS=key1=base64pubkey;key2=base64pubkey`
    pub fn from_env() -> Self {
        let mut policy = Self::default();
        if let Ok(raw) = std::env::var("MTI_COMPONENT_PUBLIC_KEYS") {
            for pair in raw.split(';') {
                let pair = pair.trim();
                if pair.is_empty() {
                    continue;
                }
                let Some((key_id, encoded_key)) = pair.split_once('=') else {
                    continue;
                };
                let key_id = key_id.trim();
                let encoded_key = encoded_key.trim();
                if key_id.is_empty() || encoded_key.is_empty() {
                    continue;
                }
                if let Ok(bytes) = BASE64_STD.decode(encoded_key) {
                    if bytes.len() == 32 {
                        let mut arr = [0u8; 32];
                        arr.copy_from_slice(&bytes);
                        if let Ok(key) = VerifyingKey::from_bytes(&arr) {
                            policy.public_keys.insert(key_id.to_string(), key);
                        }
                    }
                }
            }
        }
        policy
    }
}

impl ComponentDownloadPolicy {
    /// Build policy from environment variables.
    ///
    /// - `MTI_COMPONENT_DOWNLOAD_ATTEMPTS` (default: 3)
    /// - `MTI_COMPONENT_DOWNLOAD_TIMEOUT_SECS` (default: 30)
    /// - `MTI_COMPONENT_HOST_ALLOWLIST` semicolon-separated hosts
    pub fn from_env() -> Self {
        let mut policy = Self::default();
        if let Ok(v) = std::env::var("MTI_COMPONENT_DOWNLOAD_ATTEMPTS") {
            if let Ok(n) = v.parse::<u32>() {
                policy.attempts = n.max(1);
            }
        }
        if let Ok(v) = std::env::var("MTI_COMPONENT_DOWNLOAD_TIMEOUT_SECS") {
            if let Ok(n) = v.parse::<u64>() {
                policy.timeout = Duration::from_secs(n.max(1));
            }
        }
        if let Ok(v) = std::env::var("MTI_COMPONENT_HOST_ALLOWLIST") {
            policy.allowed_hosts = v
                .split(';')
                .map(|s| s.trim().to_ascii_lowercase())
                .filter(|s| !s.is_empty())
                .collect();
        }
        policy
    }
}

/// Load and parse component manifest from YAML.
pub fn load_component_manifest(path: &Path) -> Result<ComponentManifest> {
    let content = fs::read_to_string(path).map_err(|e| {
        InstallerError::Config(format!(
            "Failed to read component manifest '{}': {}",
            path.display(),
            e
        ))
    })?;
    serde_yaml::from_str(&content).map_err(|e| {
        InstallerError::Config(format!(
            "Failed to parse component manifest '{}': {}",
            path.display(),
            e
        ))
    })
}

/// Find component by id.
pub fn find_component<'a>(
    manifest: &'a ComponentManifest,
    component_id: &str,
) -> Result<&'a ComponentEntry> {
    manifest
        .components
        .iter()
        .find(|c| c.id == component_id)
        .ok_or_else(|| {
            InstallerError::Config(format!(
                "Component '{}' not found in manifest",
                component_id
            ))
        })
}

/// Download component package into cache directory.
///
/// Skeleton behavior:
/// - Supports `file://` URLs and plain local file paths.
/// - Returns error for HTTP/HTTPS until downloader is fully implemented.
pub fn download_component_to_cache(
    component: &ComponentEntry,
    cache_root: &Path,
    policy: &ComponentDownloadPolicy,
) -> Result<PathBuf> {
    fs::create_dir_all(cache_root)?;

    let ext = extension_from_url_or_path(&component.package.url);
    let filename = format!(
        "{}-{}.{}",
        component.id.replace('/', "_"),
        component.version.replace('/', "_"),
        ext
    );
    let target = cache_root.join(filename);

    if let Some(path) = component.package.url.strip_prefix("file://") {
        let source = PathBuf::from(path);
        if !source.exists() {
            return Err(InstallerError::Config(format!(
                "Component source file does not exist: {}",
                source.display()
            )));
        }
        fs::copy(&source, &target)?;
        return Ok(target);
    }

    let local_path = PathBuf::from(&component.package.url);
    if local_path.exists() {
        fs::copy(&local_path, &target)?;
        return Ok(target);
    }

    let url = Url::parse(&component.package.url).map_err(|e| {
        InstallerError::Config(format!(
            "Invalid component URL '{}': {}",
            component.package.url, e
        ))
    })?;
    if url.scheme() != "http" && url.scheme() != "https" {
        return Err(InstallerError::Config(format!(
            "Unsupported component URL scheme '{}' for '{}'",
            url.scheme(),
            component.package.url
        )));
    }
    enforce_host_allowlist(&url, policy)?;
    download_http_to_path(&url, &target, component.package.size, policy)?;

    Ok(target)
}

fn extension_from_url_or_path(input: &str) -> String {
    let path_like = input
        .strip_prefix("file://")
        .map(ToOwned::to_owned)
        .unwrap_or_else(|| input.to_string());
    let path = Path::new(&path_like);
    path.extension()
        .and_then(|s| s.to_str())
        .unwrap_or("bin")
        .to_string()
}

fn enforce_host_allowlist(url: &Url, policy: &ComponentDownloadPolicy) -> Result<()> {
    if policy.allowed_hosts.is_empty() {
        return Ok(());
    }
    let host = url
        .host_str()
        .ok_or_else(|| InstallerError::Config(format!("URL '{}' has no host", url)))?
        .to_ascii_lowercase();
    if policy.allowed_hosts.iter().any(|h| h == &host) {
        Ok(())
    } else {
        Err(InstallerError::PermissionDenied(format!(
            "Host '{}' is not in component download allowlist",
            host
        )))
    }
}

fn download_http_to_path(
    url: &Url,
    target: &Path,
    expected_size: u64,
    policy: &ComponentDownloadPolicy,
) -> Result<()> {
    let client = Client::builder()
        .timeout(policy.timeout)
        .build()
        .map_err(|e| InstallerError::Config(format!("Failed to build HTTP client: {}", e)))?;

    let mut last_err: Option<InstallerError> = None;
    for _ in 0..policy.attempts.max(1) {
        let response = client.get(url.clone()).send();
        match response {
            Ok(mut resp) => {
                if !resp.status().is_success() {
                    last_err = Some(InstallerError::Config(format!(
                        "HTTP {} while downloading {}",
                        resp.status(),
                        url
                    )));
                    continue;
                }

                let tmp = target.with_extension("tmp");
                let mut file = fs::File::create(&tmp)?;
                let mut downloaded: u64 = 0;
                let mut buf = [0u8; 64 * 1024];
                loop {
                    let n = resp.read(&mut buf)?;
                    if n == 0 {
                        break;
                    }
                    file.write_all(&buf[..n])?;
                    downloaded += n as u64;
                }
                file.flush()?;

                if expected_size > 0 && downloaded != expected_size {
                    let _ = fs::remove_file(&tmp);
                    last_err = Some(InstallerError::Config(format!(
                        "Downloaded size mismatch for {}: expected {}, got {}",
                        url, expected_size, downloaded
                    )));
                    continue;
                }

                fs::rename(&tmp, target)?;
                return Ok(());
            }
            Err(e) => {
                last_err = Some(InstallerError::Config(format!(
                    "Download error for {}: {}",
                    url, e
                )));
            }
        }
    }

    Err(last_err.unwrap_or_else(|| {
        InstallerError::Config(format!(
            "Failed to download {} after {} attempts",
            url, policy.attempts
        ))
    }))
}

/// Compute SHA256 hex digest for file.
pub fn sha256_file_hex(path: &Path) -> Result<String> {
    let mut file = fs::File::open(path)?;
    let mut hasher = Sha256::new();
    let mut buf = [0u8; 64 * 1024];
    loop {
        let read = file.read(&mut buf)?;
        if read == 0 {
            break;
        }
        hasher.update(&buf[..read]);
    }
    Ok(format!("{:x}", hasher.finalize()))
}

/// Verify component file against expected SHA256.
pub fn verify_component_sha256(path: &Path, expected_sha256: &str) -> Result<()> {
    let expected = expected_sha256.trim().to_ascii_lowercase();
    if expected.len() != 64 || !expected.chars().all(|c| c.is_ascii_hexdigit()) {
        return Err(InstallerError::Config(format!(
            "Invalid SHA256 value '{}'",
            expected_sha256
        )));
    }

    let actual = sha256_file_hex(path)?;
    if actual != expected {
        return Err(InstallerError::Config(format!(
            "Component SHA256 mismatch: expected {}, got {}",
            expected, actual
        )));
    }
    Ok(())
}

fn signing_payload(manifest: &ComponentManifest, component: &ComponentEntry) -> Vec<u8> {
    format!(
        "version={}\nchannel={}\nkey_id={}\ncomponent_id={}\ncomponent_version={}\nurl={}\nsize={}\nsha256={}\n",
        manifest.version,
        manifest.channel.as_deref().unwrap_or(""),
        manifest.public_key_id.as_deref().unwrap_or(""),
        component.id,
        component.version,
        component.package.url,
        component.package.size,
        component.package.sha256.trim().to_ascii_lowercase()
    )
    .into_bytes()
}

/// Verify component signature if `package.signature` is present.
pub fn verify_component_signature(
    manifest: &ComponentManifest,
    component: &ComponentEntry,
    policy: &ComponentSignaturePolicy,
) -> Result<()> {
    let Some(signature_b64) = component.package.signature.as_deref() else {
        return Ok(());
    };

    let key_id = manifest.public_key_id.as_deref().ok_or_else(|| {
        InstallerError::Config(format!(
            "Component '{}' has signature but manifest.public_key_id is missing",
            component.id
        ))
    })?;
    let key = policy.public_keys.get(key_id).ok_or_else(|| {
        InstallerError::PermissionDenied(format!(
            "No public key configured for key_id '{}'",
            key_id
        ))
    })?;

    let sig_bytes = BASE64_STD.decode(signature_b64).map_err(|e| {
        InstallerError::Config(format!(
            "Invalid base64 signature for component '{}': {}",
            component.id, e
        ))
    })?;
    let signature = Signature::from_slice(&sig_bytes).map_err(|e| {
        InstallerError::Config(format!(
            "Invalid Ed25519 signature bytes for component '{}': {}",
            component.id, e
        ))
    })?;

    let payload = signing_payload(manifest, component);
    key.verify(&payload, &signature).map_err(|e| {
        InstallerError::PermissionDenied(format!(
            "Signature verification failed for component '{}': {}",
            component.id, e
        ))
    })?;
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use ed25519_dalek::{Signer, SigningKey};
    use tempfile::tempdir;

    #[test]
    fn manifest_parse_works() {
        let yaml = r#"
version: 1
components:
  - id: extra
    display_name: Extra
    version: "1.0.0"
    package:
      url: "file:///tmp/extra.zip"
      size: 1
      sha256: "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
"#;
        let manifest: ComponentManifest = serde_yaml::from_str(yaml).expect("manifest parse");
        assert_eq!(manifest.components.len(), 1);
        assert_eq!(manifest.components[0].id, "extra");
    }

    #[test]
    fn download_and_verify_local_file() {
        let dir = tempdir().expect("tempdir");
        let source = dir.path().join("payload.bin");
        fs::write(&source, b"hello component").expect("write source");
        let sha = sha256_file_hex(&source).expect("sha");

        let entry = ComponentEntry {
            id: "extra".to_string(),
            display_name: "Extra".to_string(),
            version: "1.0.0".to_string(),
            required: false,
            package: ComponentPackage {
                url: format!("file://{}", source.to_string_lossy()),
                size: 15,
                sha256: sha.clone(),
                signature: None,
            },
            install: None,
            rollback: None,
        };

        let cache = dir.path().join("cache");
        let policy = ComponentDownloadPolicy::default();
        let downloaded = download_component_to_cache(&entry, &cache, &policy).expect("download");
        verify_component_sha256(&downloaded, &sha).expect("verify");
    }

    #[test]
    fn host_allowlist_blocks_unlisted_hosts() {
        let entry = ComponentEntry {
            id: "extra".to_string(),
            display_name: "Extra".to_string(),
            version: "1.0.0".to_string(),
            required: false,
            package: ComponentPackage {
                url: "https://example.com/file.zip".to_string(),
                size: 1,
                sha256: "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
                    .to_string(),
                signature: None,
            },
            install: None,
            rollback: None,
        };
        let dir = tempdir().expect("tempdir");
        let policy = ComponentDownloadPolicy {
            attempts: 1,
            timeout: Duration::from_secs(1),
            allowed_hosts: vec!["downloads.example.com".to_string()],
        };
        let err =
            download_component_to_cache(&entry, dir.path(), &policy).expect_err("should block");
        assert!(matches!(err, InstallerError::PermissionDenied(_)));
    }

    #[test]
    fn signature_verification_works() {
        let signing_key = SigningKey::from_bytes(&[7u8; 32]);
        let verify_key = signing_key.verifying_key();

        let mut policy = ComponentSignaturePolicy::default();
        policy
            .public_keys
            .insert("test-key".to_string(), verify_key);

        let mut manifest = ComponentManifest {
            version: 1,
            channel: Some("stable".to_string()),
            public_key_id: Some("test-key".to_string()),
            components: Vec::new(),
        };
        let mut component = ComponentEntry {
            id: "extra-tools".to_string(),
            display_name: "Extra Tools".to_string(),
            version: "1.0.0".to_string(),
            required: false,
            package: ComponentPackage {
                url: "https://downloads.example.com/extra-tools.zip".to_string(),
                size: 42,
                sha256: "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
                    .to_string(),
                signature: None,
            },
            install: None,
            rollback: None,
        };

        let payload = signing_payload(&manifest, &component);
        let sig = signing_key.sign(&payload);
        component.package.signature = Some(BASE64_STD.encode(sig.to_bytes()));
        manifest.components.push(component.clone());

        verify_component_signature(&manifest, &component, &policy)
            .expect("signature should verify");

        component.package.sha256 =
            "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb".to_string();
        let err =
            verify_component_signature(&manifest, &component, &policy).expect_err("should fail");
        assert!(matches!(err, InstallerError::PermissionDenied(_)));
    }
}
