//! Installer module for extracting and installing packages.
//!
//! Provides functionality for:
//! - Parsing installer packages
//! - Checking system requirements (disk space, Windows version, running processes)
//! - Installing files with parallel decompression
//! - Rolling back failed installations
//! - Creating uninstallers
//!
//! # Requirements
//! - 3.1: Parse embedded package format
//! - 3.9: Support parallel decompression using thread pool
//! - 3.10: Report success or failure with error details
//! - 7.1: Emit progress events during file processing
//! - 8.5, 8.6, 8.7: Rollback on failure
//! - 11.1, 11.2: Create uninstaller

use crate::components::installer as component_installer;
use crate::components::runtime::ComponentRuntimeState;
use crate::components::{ComponentDownloadPolicy, ComponentSignaturePolicy};
use crate::compression::{decompress, verify_crc32};
use crate::filesystem::{
    check_disk_space, create_dir_all, delete_file, set_file_permissions_public, write_file,
};
use crate::flow_executor::{FlowContext, FlowExecutor, FlowRuntime};
use crate::flow_steps::{BuiltinStepRuntime, StepRegistry};
use crate::package::{footer_size, read_footer, read_header, read_metadata, read_toc, Toc};
use crate::platform::{create_platform, Platform, UninstallInfo};
use crate::script_executor::{ScriptExecutor, ScriptPolicy};
use crate::ui_resources::UIResources;
use installer_shared::{
    FlowDefinition, FlowStep, InstallOptions, InstallerError, PackageMetadata, Phase,
    ProgressEvent, Result,
};
use rayon::prelude::*;
use serde_json::{json, Value};
use std::fs::File;
use std::io::{BufReader, Read, Seek, SeekFrom, Write};
use std::path::{Component, Path, PathBuf};
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::{Arc, OnceLock};
use std::time::{Duration, Instant};
use tracing::{debug, error, info, warn};

/// Expand environment variables in a path string.
/// Supports %VAR% syntax on Windows.
fn expand_env_vars_in_path(path: &str) -> String {
    let mut result = path.to_string();

    // Common Windows environment variables to expand
    let env_vars = [
        ("ProgramFiles", "PROGRAMFILES"),
        ("ProgramFiles(x86)", "PROGRAMFILES(X86)"),
        ("LocalAppData", "LOCALAPPDATA"),
        ("AppData", "APPDATA"),
        ("UserProfile", "USERPROFILE"),
        ("SystemRoot", "SYSTEMROOT"),
        ("WinDir", "WINDIR"),
        ("Temp", "TEMP"),
        ("Tmp", "TMP"),
    ];

    for (name, env_name) in env_vars {
        // Try both %Name% and %NAME% patterns
        let patterns = [
            format!("%{}%", name),
            format!("%{}%", name.to_uppercase()),
            format!("%{}%", env_name),
        ];

        for pattern in patterns {
            if result.contains(&pattern) {
                if let Ok(value) = std::env::var(env_name) {
                    result = result.replace(&pattern, &value);
                } else if let Ok(value) = std::env::var(name) {
                    result = result.replace(&pattern, &value);
                }
            }
        }
    }

    result
}

/// Check if a path is absolute (starts with a drive letter on Windows or / on Unix).
fn is_absolute_path(path: &str) -> bool {
    #[cfg(windows)]
    {
        // Check for drive letter (e.g., C:\) or UNC path (\\server\share)
        if path.len() >= 2 {
            let bytes = path.as_bytes();
            // Drive letter pattern: C:\ or C:/
            if bytes[1] == b':' && (bytes.len() < 3 || bytes[2] == b'\\' || bytes[2] == b'/') {
                return true;
            }
            // UNC path pattern: \\server or //server
            if (bytes[0] == b'\\' && bytes[1] == b'\\') || (bytes[0] == b'/' && bytes[1] == b'/') {
                return true;
            }
        }
        false
    }
    #[cfg(not(windows))]
    {
        path.starts_with('/')
    }
}

/// Verify package-level CRC32 (excludes footer).
fn verify_package_crc32(path: &Path, expected: u32) -> Result<()> {
    let file = File::open(path)?;
    let file_size = file.metadata()?.len();
    let footer_len = footer_size() as u64;

    if file_size < footer_len {
        return Err(InstallerError::InvalidFormat(
            "Package file too small to contain footer".to_string(),
        ));
    }

    let data_len = file_size - footer_len;
    let mut reader = BufReader::new(file);
    let mut hasher = crc32fast::Hasher::new();
    let mut remaining = data_len;
    let mut buffer = [0u8; 64 * 1024];

    while remaining > 0 {
        let to_read = buffer.len().min(remaining as usize);
        reader.read_exact(&mut buffer[..to_read])?;
        hasher.update(&buffer[..to_read]);
        remaining -= to_read as u64;
    }

    let actual = hasher.finalize();
    if actual != expected {
        return Err(InstallerError::ChecksumMismatch { expected, actual });
    }

    Ok(())
}

/// Statistics about an installation.
#[derive(Debug, Clone)]
pub struct InstallStats {
    /// Number of files installed
    pub installed_files: usize,
    /// Total size of installed files
    pub total_size: u64,
    /// Time taken for installation
    pub elapsed_time: Duration,
}

/// Parsed package information.
#[derive(Debug)]
pub struct ParsedPackage {
    /// Package metadata
    pub metadata: PackageMetadata,
    /// Table of contents
    pub toc: Toc,
    /// Data section offset
    pub data_offset: u64,
    /// UI resources offset (0 if not present)
    pub ui_resources_offset: u64,
    /// UI resources size (0 if not present)
    pub ui_resources_size: u64,
    /// Whether the package has UI resources
    pub has_ui_resources: bool,
}

/// Installer for extracting and installing packages.
pub struct Installer {
    package_path: PathBuf,
    platform: Box<dyn Platform>,
    script_policy: ScriptPolicy,
    component_download_policy: ComponentDownloadPolicy,
    component_signature_policy: ComponentSignaturePolicy,
}

struct InstallFlowRuntime<'a, F>
where
    F: Fn(ProgressEvent) + Send + Sync,
{
    installer: &'a Installer,
    parsed: &'a ParsedPackage,
    options: &'a InstallOptions,
    progress: &'a F,
    installed_files: Vec<PathBuf>,
    component_state: ComponentRuntimeState,
}

impl<'a, F> InstallFlowRuntime<'a, F>
where
    F: Fn(ProgressEvent) + Send + Sync,
{
    fn new(
        installer: &'a Installer,
        parsed: &'a ParsedPackage,
        options: &'a InstallOptions,
        progress: &'a F,
    ) -> Self {
        Self {
            installer,
            parsed,
            options,
            progress,
            installed_files: Vec::new(),
            component_state: ComponentRuntimeState::new(options.install_dir.clone()),
        }
    }
}

impl<F> FlowRuntime for InstallFlowRuntime<'_, F>
where
    F: Fn(ProgressEvent) + Send + Sync,
{
    fn execute_builtin(&mut self, step: &FlowStep, context: &mut FlowContext) -> Result<()> {
        static STEP_REGISTRY: OnceLock<StepRegistry> = OnceLock::new();
        let registry = STEP_REGISTRY.get_or_init(StepRegistry::new);
        registry.execute(self, step, context)
    }

    fn execute_script(&mut self, step: &FlowStep, context: &mut FlowContext) -> Result<()> {
        self.installer.execute_script_step(step, context)
    }
}

impl<F> BuiltinStepRuntime for InstallFlowRuntime<'_, F>
where
    F: Fn(ProgressEvent) + Send + Sync,
{
    fn check_disk(&mut self) -> Result<()> {
        self.installer
            .check_disk_space_for_parsed(self.parsed, &self.options.install_dir)
    }

    fn extract_package(&mut self) -> Result<()> {
        self.installer.extract_payload(
            self.parsed,
            self.options,
            self.progress,
            &mut self.installed_files,
        )
    }

    fn create_shortcut(&mut self) -> Result<()> {
        self.installer
            .create_shortcut_if_needed(self.parsed, self.options)
    }

    fn write_registry(&mut self) -> Result<()> {
        self.installer
            .configure_registry_if_needed(self.parsed, self.options)
    }

    fn configure_autostart(&mut self) -> Result<()> {
        self.installer
            .configure_autostart_if_needed(self.parsed, self.options)
    }

    fn load_component_manifest(&mut self, step: &FlowStep) -> Result<()> {
        self.installer
            .load_component_manifest_step(step, &mut self.component_state)
    }

    fn resolve_selected_components(
        &mut self,
        step: &FlowStep,
        context: &mut FlowContext,
    ) -> Result<()> {
        self.installer
            .resolve_selected_components_step(step, &mut self.component_state, context)
    }

    fn process_selected_components(
        &mut self,
        step: &FlowStep,
        context: &mut FlowContext,
    ) -> Result<()> {
        self.installer
            .process_selected_components_step(step, &mut self.component_state, context)
    }

    fn download_component(&mut self, step: &FlowStep, context: &mut FlowContext) -> Result<()> {
        self.installer
            .download_component_step(step, &mut self.component_state, context)
    }

    fn verify_component(&mut self, step: &FlowStep, context: &mut FlowContext) -> Result<()> {
        self.installer
            .verify_component_step(step, &mut self.component_state, context)
    }

    fn install_component(&mut self, step: &FlowStep, context: &mut FlowContext) -> Result<()> {
        self.installer
            .install_component_step(step, &mut self.component_state, context)
    }

    fn rollback_component(&mut self) -> Result<()> {
        self.installer
            .rollback_component_step(&mut self.component_state)
    }

    fn rollback_files(&mut self) -> Result<()> {
        self.installer.rollback(&self.installed_files)
    }
}

impl Installer {
    fn parse_default_install_flow_yaml(yaml: &str) -> Result<FlowDefinition> {
        FlowDefinition::from_yaml_str(yaml).map_err(|e| {
            InstallerError::Config(format!(
                "Failed to parse built-in default install flow YAML: {}",
                e
            ))
        })
    }

    fn build_default_install_flow_definition() -> FlowDefinition {
        const DEFAULT_FLOW_YAML: &str = include_str!("flows/default_install_flow.yaml");
        Self::parse_default_install_flow_yaml(DEFAULT_FLOW_YAML).expect(
            "Failed to parse built-in default install flow YAML; check installer_core/src/flows/default_install_flow.yaml",
        )
    }

    fn build_flow_context(
        &self,
        flow: &FlowDefinition,
        parsed: &ParsedPackage,
        options: &InstallOptions,
        embedded_scripts_root: Option<&Path>,
    ) -> FlowContext {
        let mut context = FlowContext::from_definition(flow);
        context.metadata = json!({
            "app_name": parsed.metadata.app_name,
            "version": parsed.metadata.version,
            "desktop_icons": parsed.metadata.desktop_icons,
            "auto_startup": parsed.metadata.auto_startup,
        });
        context.options = json!({
            "install_dir": options.install_dir.to_string_lossy().to_string(),
            "create_shortcuts": options.create_shortcuts,
            "configure_registry": options.configure_registry,
            "auto_startup": options.auto_startup,
            "components": &options.components,
            "silent": options.silent,
        });
        context.set_var(
            "InstallDir",
            Value::String(options.install_dir.to_string_lossy().to_string()),
        );
        if let Some(root) = embedded_scripts_root {
            context.set_var(
                "__embedded_scripts_root",
                Value::String(root.to_string_lossy().to_string()),
            );
        }
        context
    }

    /// Create a new installer for the given package.
    pub fn new(package_path: PathBuf) -> Result<Self> {
        if !package_path.exists() {
            return Err(InstallerError::Io(std::io::Error::new(
                std::io::ErrorKind::NotFound,
                format!("Package not found: {:?}", package_path),
            )));
        }

        Ok(Self {
            package_path,
            platform: create_platform(),
            script_policy: ScriptPolicy::from_env(),
            component_download_policy: ComponentDownloadPolicy::from_env(),
            component_signature_policy: ComponentSignaturePolicy::from_env(),
        })
    }

    /// Override script execution policy for this installer instance.
    pub fn with_script_policy(mut self, policy: ScriptPolicy) -> Self {
        self.script_policy = policy;
        self
    }

    /// Override component download policy for this installer instance.
    pub fn with_component_download_policy(mut self, policy: ComponentDownloadPolicy) -> Self {
        self.component_download_policy = policy;
        self
    }

    /// Override component signature verification policy for this installer instance.
    pub fn with_component_signature_policy(mut self, policy: ComponentSignaturePolicy) -> Self {
        self.component_signature_policy = policy;
        self
    }

    /// Parse the package and return its contents.
    pub fn parse_package(&self) -> Result<ParsedPackage> {
        info!("Parsing package: {:?}", self.package_path);

        let file = File::open(&self.package_path)?;
        let mut reader = BufReader::new(file);

        // Read header
        let header = read_header(&mut reader)?;
        debug!("Header: version={}, flags={}", header.version, header.flags);

        // Read TOC
        reader.seek(SeekFrom::Start(header.toc_offset))?;
        let toc = read_toc(&mut reader, header.toc_size as usize)?;
        debug!(
            "TOC: {} files, {} blocks",
            toc.header.file_count, toc.header.block_count
        );

        // Read metadata
        reader.seek(SeekFrom::Start(header.metadata_offset))?;
        let metadata = read_metadata(&mut reader, header.metadata_size as usize)?;
        debug!(
            "Metadata: app={}, version={}",
            metadata.app_name, metadata.version
        );

        // Verify package CRC32 if present
        let footer = read_footer(&mut reader)?;
        if footer.crc32 != 0 {
            verify_package_crc32(&self.package_path, footer.crc32)?;
        }

        Ok(ParsedPackage {
            metadata,
            toc,
            data_offset: header.data_offset,
            ui_resources_offset: header.ui_resources_offset,
            ui_resources_size: header.ui_resources_size,
            has_ui_resources: header.has_ui_resources(),
        })
    }

    /// Check if there is sufficient disk space.
    pub fn check_disk_space(&self, install_dir: &Path) -> Result<()> {
        let parsed = self.parse_package()?;
        self.check_disk_space_for_parsed(&parsed, install_dir)
    }

    fn check_disk_space_for_parsed(
        &self,
        parsed: &ParsedPackage,
        install_dir: &Path,
    ) -> Result<()> {
        let required: u64 = parsed.toc.files.iter().map(|f| f.original_size).sum();
        let buffer = 100 * 1024 * 1024; // 100MB buffer
        check_disk_space(install_dir, required, buffer)
    }

    /// Check Windows version requirements.
    pub fn check_windows_version(&self) -> Result<()> {
        let parsed = self.parse_package()?;

        if let Some(min_version) = parsed.metadata.min_windows_version {
            let current = self.platform.windows_version()?;

            if current.major < min_version.major
                || (current.major == min_version.major && current.minor < min_version.minor)
                || (current.major == min_version.major
                    && current.minor == min_version.minor
                    && current.build < min_version.build)
            {
                return Err(InstallerError::VersionCheckFailed(format!(
                    "Windows {}.{}.{} required, but {}.{}.{} found",
                    min_version.major,
                    min_version.minor,
                    min_version.build,
                    current.major,
                    current.minor,
                    current.build
                )));
            }
        }

        Ok(())
    }

    /// Check if the target process is running.
    pub fn check_running_process(&self) -> Result<bool> {
        let parsed = self.parse_package()?;

        if let Some(ref process_name) = parsed.metadata.process_name {
            return self.platform.is_process_running(process_name);
        }

        Ok(false)
    }

    /// Extract UI resources from the package to a temporary directory.
    ///
    /// Reads the embedded UI resources from the package, verifies the checksum,
    /// and extracts them to the specified directory.
    ///
    /// # Arguments
    /// * `temp_dir` - Directory to extract UI resources to
    ///
    /// # Returns
    /// * `Ok(Some(UIResources))` if UI resources were extracted
    /// * `Ok(None)` if the package doesn't contain UI resources
    /// * `Err(InstallerError)` on extraction failure
    ///
    /// # Requirements
    /// - 5.4: Extract UI resources to temporary directory
    /// - 5.9: Verify UI resources integrity using checksum
    pub fn extract_ui_resources(&self, temp_dir: &Path) -> Result<Option<UIResources>> {
        let parsed = self.parse_package()?;

        if !parsed.has_ui_resources || parsed.ui_resources_size == 0 {
            info!("Package does not contain UI resources");
            return Ok(None);
        }

        info!(
            "Extracting UI resources ({} bytes) to {:?}",
            parsed.ui_resources_size, temp_dir
        );

        // Read UI resources from package
        let file = File::open(&self.package_path)?;
        let mut reader = BufReader::new(file);

        reader.seek(SeekFrom::Start(parsed.ui_resources_offset))?;
        let mut archive_data = vec![0u8; parsed.ui_resources_size as usize];
        reader.read_exact(&mut archive_data)?;

        // Get expected checksum from metadata
        let expected_checksum = parsed.metadata.ui_resources_checksum.ok_or_else(|| {
            InstallerError::InvalidFormat(
                "Package has UI resources flag but no checksum in metadata".to_string(),
            )
        })?;

        // Create UIResources and verify checksum
        let ui_resources =
            UIResources::from_archive(archive_data, expected_checksum, parsed.ui_resources_size)?;

        // Extract to temp directory
        ui_resources.extract_to(temp_dir)?;

        info!(
            "Extracted UI resources: {} locales available",
            ui_resources.locales.len()
        );

        Ok(Some(ui_resources))
    }

    /// Check if the package contains UI resources.
    pub fn has_ui_resources(&self) -> Result<bool> {
        let parsed = self.parse_package()?;
        Ok(parsed.has_ui_resources)
    }

    fn extract_payload<F>(
        &self,
        parsed: &ParsedPackage,
        options: &InstallOptions,
        progress: &F,
        installed_files: &mut Vec<PathBuf>,
    ) -> Result<()>
    where
        F: Fn(ProgressEvent) + Send + Sync,
    {
        create_dir_all(&options.install_dir)?;

        let file = File::open(&self.package_path)?;
        let mut reader = BufReader::new(file);
        let total_blocks = parsed.toc.blocks.len() as u64;
        let processed = Arc::new(AtomicU64::new(0));
        let pool = if let Some(thread_count) = options.thread_count {
            rayon::ThreadPoolBuilder::new()
                .num_threads(thread_count)
                .build()
                .ok()
        } else {
            None
        };
        let window_blocks = options.thread_count.map(|n| n.max(1) * 2).unwrap_or(8);

        progress(ProgressEvent::new(
            Phase::Writing,
            0,
            parsed.toc.files.len() as u64,
        ));

        let mut files_written = 0u64;
        let mut current_file_index = 0usize;
        let mut current_file: Option<(std::fs::File, PathBuf, u32, crc32fast::Hasher, u64)> = None;

        struct BlockWork {
            index: usize,
            entry: installer_shared::BlockEntry,
            data: Vec<u8>,
        }

        struct DecompressedBlock {
            index: usize,
            data: Vec<u8>,
        }

        let mut block_index = 0usize;
        while block_index < parsed.toc.blocks.len() {
            let end = (block_index + window_blocks).min(parsed.toc.blocks.len());
            let mut work_items = Vec::with_capacity(end - block_index);

            for (offset, block_entry) in parsed.toc.blocks[block_index..end].iter().enumerate() {
                reader.seek(SeekFrom::Start(parsed.data_offset + block_entry.offset))?;
                let mut compressed_data = vec![0u8; block_entry.compressed_size as usize];
                reader.read_exact(&mut compressed_data)?;
                work_items.push(BlockWork {
                    index: block_index + offset,
                    entry: *block_entry,
                    data: compressed_data,
                });
            }

            let processed = Arc::clone(&processed);
            let decompress_fn = |items: &[BlockWork]| {
                items
                    .par_iter()
                    .map(|item| {
                        verify_crc32(&item.data, item.entry.checksum)?;
                        let decompressed = decompress(&item.data, item.entry.algorithm)?;
                        let count = processed.fetch_add(1, Ordering::SeqCst) + 1;
                        progress(ProgressEvent::new(
                            Phase::Decompressing,
                            count,
                            total_blocks,
                        ));
                        Ok(DecompressedBlock {
                            index: item.index,
                            data: decompressed,
                        })
                    })
                    .collect::<Vec<Result<DecompressedBlock>>>()
            };

            let decompressed_blocks: Vec<Result<DecompressedBlock>> = if let Some(pool) = &pool {
                pool.install(|| decompress_fn(&work_items))
            } else {
                decompress_fn(&work_items)
            };

            let mut decompressed_blocks: Vec<DecompressedBlock> = decompressed_blocks
                .into_iter()
                .collect::<Result<Vec<_>>>()?;
            decompressed_blocks.sort_by_key(|block| block.index);

            for decompressed_block in decompressed_blocks {
                let decompressed = decompressed_block.data;
                let mut offset = 0usize;
                while offset < decompressed.len() {
                    if current_file.is_none() {
                        if current_file_index >= parsed.toc.files.len() {
                            return Err(InstallerError::InvalidFormat(
                                "Decompressed data exceeds file table".to_string(),
                            ));
                        }

                        let file_entry = &parsed.toc.files[current_file_index];
                        let expanded_path = expand_env_vars_in_path(&file_entry.path);
                        let file_path = if is_absolute_path(&expanded_path) {
                            PathBuf::from(&expanded_path)
                        } else {
                            options.install_dir.join(&expanded_path)
                        };

                        if let Some(parent) = file_path.parent() {
                            create_dir_all(parent)?;
                        }

                        let output_file = std::fs::File::create(&file_path)?;
                        current_file = Some((
                            output_file,
                            file_path,
                            file_entry.mode,
                            crc32fast::Hasher::new(),
                            file_entry.original_size,
                        ));
                    }

                    let (ref mut output_file, _path, _mode, ref mut hasher, remaining) =
                        current_file.as_mut().unwrap();
                    let remaining_usize = *remaining as usize;

                    if remaining_usize == 0 {
                        let (mut output_file, path, mode, hasher, _remaining) =
                            current_file.take().unwrap();
                        output_file.flush()?;
                        set_file_permissions_public(&path, mode)?;
                        let actual_checksum = hasher.finalize();
                        let expected_checksum = parsed.toc.files[current_file_index].checksum;
                        if actual_checksum != expected_checksum {
                            return Err(InstallerError::ChecksumMismatch {
                                expected: expected_checksum,
                                actual: actual_checksum,
                            });
                        }
                        installed_files.push(path);
                        files_written += 1;
                        progress(
                            ProgressEvent::new(
                                Phase::Writing,
                                files_written,
                                parsed.toc.files.len() as u64,
                            )
                            .with_file(&parsed.toc.files[current_file_index].path),
                        );
                        current_file_index += 1;
                        continue;
                    }

                    let available = decompressed.len() - offset;
                    let take = available.min(remaining_usize);
                    let slice = &decompressed[offset..offset + take];

                    output_file.write_all(slice)?;
                    hasher.update(slice);

                    offset += take;
                    *remaining -= take as u64;

                    if *remaining == 0 {
                        let (mut output_file, path, mode, hasher, _remaining) =
                            current_file.take().unwrap();
                        output_file.flush()?;
                        set_file_permissions_public(&path, mode)?;
                        let actual_checksum = hasher.finalize();
                        let expected_checksum = parsed.toc.files[current_file_index].checksum;
                        if actual_checksum != expected_checksum {
                            return Err(InstallerError::ChecksumMismatch {
                                expected: expected_checksum,
                                actual: actual_checksum,
                            });
                        }
                        installed_files.push(path);
                        files_written += 1;
                        progress(
                            ProgressEvent::new(
                                Phase::Writing,
                                files_written,
                                parsed.toc.files.len() as u64,
                            )
                            .with_file(&parsed.toc.files[current_file_index].path),
                        );
                        current_file_index += 1;
                    }
                }
            }

            block_index = end;
        }

        while current_file.is_none() && current_file_index < parsed.toc.files.len() {
            let file_entry = &parsed.toc.files[current_file_index];
            if file_entry.original_size != 0 {
                break;
            }

            let expanded_path = expand_env_vars_in_path(&file_entry.path);
            let file_path = if is_absolute_path(&expanded_path) {
                PathBuf::from(&expanded_path)
            } else {
                options.install_dir.join(&expanded_path)
            };

            if let Some(parent) = file_path.parent() {
                create_dir_all(parent)?;
            }

            let mut output_file = std::fs::File::create(&file_path)?;
            output_file.flush()?;
            set_file_permissions_public(&file_path, file_entry.mode)?;

            let actual_checksum = crc32fast::hash(&[]);
            if actual_checksum != file_entry.checksum {
                return Err(InstallerError::ChecksumMismatch {
                    expected: file_entry.checksum,
                    actual: actual_checksum,
                });
            }

            installed_files.push(file_path);
            files_written += 1;
            progress(
                ProgressEvent::new(Phase::Writing, files_written, parsed.toc.files.len() as u64)
                    .with_file(&file_entry.path),
            );
            current_file_index += 1;
        }

        if current_file_index != parsed.toc.files.len() {
            return Err(InstallerError::InvalidFormat(
                "Package data ended before all files were written".to_string(),
            ));
        }

        Ok(())
    }

    fn create_shortcut_if_needed(
        &self,
        parsed: &ParsedPackage,
        options: &InstallOptions,
    ) -> Result<()> {
        if !(options.create_shortcuts && parsed.metadata.desktop_icons) {
            return Ok(());
        }

        let exe_name = parsed
            .toc
            .files
            .iter()
            .find(|f| f.path.ends_with(".exe"))
            .map(|f| f.path.clone())
            .unwrap_or_else(|| format!("{}.exe", parsed.metadata.app_name));

        let icon_path = parsed
            .metadata
            .icon_path
            .as_ref()
            .map(|p| options.install_dir.join(p));

        self.platform
            .create_shortcut(
                &parsed.metadata.app_name,
                &options.install_dir.join(&exe_name),
                icon_path.as_deref(),
            )
            .map_err(|e| InstallerError::Platform(e.to_string()))
    }

    fn configure_registry_if_needed(
        &self,
        parsed: &ParsedPackage,
        options: &InstallOptions,
    ) -> Result<()> {
        if !options.configure_registry {
            return Ok(());
        }

        self.platform
            .register_uninstaller(&UninstallInfo {
                app_name: parsed.metadata.app_name.clone(),
                version: parsed.metadata.version.clone(),
                install_location: options.install_dir.clone(),
                uninstall_exe: options.install_dir.join("uninstall.exe"),
                publisher: parsed.metadata.vendor.clone(),
                estimated_size_kb: parsed
                    .toc
                    .files
                    .iter()
                    .map(|f| f.original_size)
                    .sum::<u64>()
                    / 1024,
            })
            .map_err(|e| InstallerError::Platform(e.to_string()))?;

        for entry in &parsed.metadata.registry_entries {
            self.platform
                .write_registry(entry)
                .map_err(|e| InstallerError::Platform(e.to_string()))?;
        }

        Ok(())
    }

    fn configure_autostart_if_needed(
        &self,
        parsed: &ParsedPackage,
        options: &InstallOptions,
    ) -> Result<()> {
        if !(options.auto_startup && parsed.metadata.auto_startup) {
            return Ok(());
        }

        let exe_name = parsed
            .toc
            .files
            .iter()
            .find(|f| f.path.ends_with(".exe"))
            .map(|f| f.path.clone())
            .unwrap_or_else(|| format!("{}.exe", parsed.metadata.app_name));

        self.platform
            .configure_auto_startup(
                &parsed.metadata.app_name,
                &options.install_dir.join(&exe_name),
                true,
            )
            .map_err(|e| InstallerError::Platform(e.to_string()))
    }

    fn load_component_manifest_step(
        &self,
        step: &FlowStep,
        state: &mut ComponentRuntimeState,
    ) -> Result<()> {
        component_installer::load_component_manifest_step(self, step, state)
    }

    fn download_component_step(
        &self,
        step: &FlowStep,
        state: &mut ComponentRuntimeState,
        context: &mut FlowContext,
    ) -> Result<()> {
        component_installer::download_component_step(
            step,
            state,
            context,
            &self.component_download_policy,
        )
    }

    fn resolve_selected_components_step(
        &self,
        step: &FlowStep,
        state: &mut ComponentRuntimeState,
        context: &mut FlowContext,
    ) -> Result<()> {
        component_installer::resolve_selected_components_step(step, state, context)
    }

    fn process_selected_components_step(
        &self,
        step: &FlowStep,
        state: &mut ComponentRuntimeState,
        context: &mut FlowContext,
    ) -> Result<()> {
        component_installer::process_selected_components_step(
            step,
            state,
            context,
            &self.component_download_policy,
            &self.component_signature_policy,
            Self::validate_embedded_script_path,
        )
    }

    fn verify_component_step(
        &self,
        step: &FlowStep,
        state: &mut ComponentRuntimeState,
        context: &mut FlowContext,
    ) -> Result<()> {
        component_installer::verify_component_step(
            step,
            state,
            context,
            &self.component_signature_policy,
        )
    }

    fn install_component_step(
        &self,
        step: &FlowStep,
        state: &mut ComponentRuntimeState,
        context: &mut FlowContext,
    ) -> Result<()> {
        component_installer::install_component_step(
            step,
            state,
            context,
            Self::validate_embedded_script_path,
        )
    }

    fn rollback_component_step(&self, state: &mut ComponentRuntimeState) -> Result<()> {
        component_installer::rollback_component_step(state)
    }

    fn execute_script_step(&self, step: &FlowStep, context: &mut FlowContext) -> Result<()> {
        ScriptExecutor::execute(step, context, &self.script_policy)
    }

    fn embedded_flow_from_package(&self, parsed: &ParsedPackage) -> Result<Option<FlowDefinition>> {
        let Some(yaml) = parsed.metadata.embedded_flow_yaml.as_deref() else {
            return Ok(None);
        };
        let flow = FlowDefinition::from_yaml_str(yaml).map_err(|e| {
            InstallerError::Config(format!("Failed to parse embedded flow from package: {e}"))
        })?;
        Ok(Some(flow))
    }

    fn validate_embedded_script_path(path: &str) -> Result<PathBuf> {
        let raw = Path::new(path);
        if raw.as_os_str().is_empty() || raw.is_absolute() {
            return Err(InstallerError::Config(format!(
                "Invalid embedded script path '{}'",
                path
            )));
        }

        let mut sanitized = PathBuf::new();
        for comp in raw.components() {
            match comp {
                Component::Normal(seg) => sanitized.push(seg),
                Component::CurDir => {}
                Component::ParentDir | Component::RootDir | Component::Prefix(_) => {
                    return Err(InstallerError::Config(format!(
                        "Embedded script path '{}' contains forbidden segments",
                        path
                    )))
                }
            }
        }
        if sanitized.as_os_str().is_empty() {
            return Err(InstallerError::Config(format!(
                "Invalid embedded script path '{}'",
                path
            )));
        }
        Ok(sanitized)
    }

    fn materialize_embedded_scripts(&self, parsed: &ParsedPackage) -> Result<Option<PathBuf>> {
        if parsed.metadata.embedded_scripts.is_empty() {
            return Ok(None);
        }

        let mut root = std::env::temp_dir();
        root.push(format!(
            "mti_embedded_scripts_{}_{}",
            std::process::id(),
            chrono::Utc::now().timestamp_nanos_opt().unwrap_or_default()
        ));
        create_dir_all(&root)?;

        for script in &parsed.metadata.embedded_scripts {
            let relative = Self::validate_embedded_script_path(&script.path)?;
            let target = root.join(relative);
            if let Some(parent) = target.parent() {
                create_dir_all(parent)?;
            }
            std::fs::write(&target, script.content.as_bytes())?;
        }

        Ok(Some(root))
    }

    /// Install the package.
    ///
    /// Uses rayon for parallel decompression of blocks while maintaining
    /// correct file order during writing.
    ///
    /// # Arguments
    /// * `options` - Installation options
    /// * `progress` - Progress callback function
    ///
    /// # Returns
    /// * `Ok(InstallStats)` on success
    /// * `Err(InstallerError)` on failure (triggers rollback)
    ///
    /// # Requirements
    /// - 3.9: Parallel decompression using thread pool
    /// - 3.10: Report success or failure
    /// - 7.1: Emit progress events
    pub fn install<F>(&self, options: InstallOptions, progress: F) -> Result<InstallStats>
    where
        F: Fn(ProgressEvent) + Send + Sync,
    {
        let parsed = self.parse_package()?;
        let flow = self
            .embedded_flow_from_package(&parsed)?
            .unwrap_or_else(Self::build_default_install_flow_definition);
        self.install_with_flow_definition(options, flow, progress)
    }

    /// Install using a custom flow definition.
    pub fn install_with_flow_definition<F>(
        &self,
        options: InstallOptions,
        flow: FlowDefinition,
        progress: F,
    ) -> Result<InstallStats>
    where
        F: Fn(ProgressEvent) + Send + Sync,
    {
        let start_time = Instant::now();
        info!("Starting installation to {:?}", options.install_dir);

        let parsed = self.parse_package()?;
        let embedded_scripts_root = self.materialize_embedded_scripts(&parsed)?;
        let mut context =
            self.build_flow_context(&flow, &parsed, &options, embedded_scripts_root.as_deref());
        let executor = FlowExecutor::new(flow)?;
        let mut runtime = InstallFlowRuntime::new(self, &parsed, &options, &progress);

        let execution_result = executor.execute(&mut runtime, &mut context);
        if let Some(root) = embedded_scripts_root {
            if let Err(e) = std::fs::remove_dir_all(&root) {
                warn!(
                    "Failed to clean embedded scripts temp dir {:?}: {}",
                    root, e
                );
            }
        }
        if let Some(cache_root) = runtime.component_state.cache_root.as_ref() {
            if let Err(e) = std::fs::remove_dir_all(cache_root) {
                warn!(
                    "Failed to clean component cache temp dir {:?}: {}",
                    cache_root, e
                );
            }
        }
        if let Err(error) = execution_result {
            error!("Installation flow failed: {}", error);
            return Err(error);
        }
        progress(ProgressEvent::new(Phase::Completing, 1, 1));

        let total_size: u64 = parsed.toc.files.iter().map(|f| f.original_size).sum();
        let elapsed_time = start_time.elapsed();

        info!(
            "Installation complete: {} files, {} bytes in {:?}",
            runtime.installed_files.len(),
            total_size,
            elapsed_time
        );

        Ok(InstallStats {
            installed_files: runtime.installed_files.len(),
            total_size,
            elapsed_time,
        })
    }

    /// Rollback a partial installation.
    ///
    /// Deletes all installed files, removes empty directories, and cleans up
    /// registry entries created during installation.
    ///
    /// # Arguments
    /// * `installed_files` - List of files that were installed
    ///
    /// # Requirements
    /// - 8.5: Delete all partially written files
    /// - 8.6: Delete created empty directories
    /// - 8.7: Report rollback success or failure
    pub fn rollback(&self, installed_files: &[PathBuf]) -> Result<()> {
        info!("Rolling back {} files", installed_files.len());

        let mut rollback_errors = Vec::new();

        // Delete installed files
        for file in installed_files {
            if let Err(e) = delete_file(file) {
                error!("Failed to delete file {:?}: {}", file, e);
                rollback_errors.push(format!("File {:?}: {}", file, e));
            }
        }

        // Try to remove empty directories (deepest first)
        let mut dirs: Vec<_> = installed_files
            .iter()
            .filter_map(|f| f.parent().map(|p| p.to_path_buf()))
            .collect();
        dirs.sort();
        dirs.dedup();
        dirs.reverse(); // Process deepest first

        for dir in dirs {
            if dir.exists()
                && dir
                    .read_dir()
                    .map(|mut d| d.next().is_none())
                    .unwrap_or(false)
            {
                if let Err(e) = std::fs::remove_dir(&dir) {
                    warn!("Failed to remove empty directory {:?}: {}", dir, e);
                } else {
                    debug!("Removed empty directory: {:?}", dir);
                }
            }
        }

        // Clean up registry entries if we have package metadata
        if let Ok(parsed) = self.parse_package() {
            // Remove custom registry entries
            for entry in &parsed.metadata.registry_entries {
                if let Err(e) = self.platform.delete_registry(&entry.path, &entry.key) {
                    warn!("Failed to delete registry entry {}: {}", entry.key, e);
                }
            }

            // Remove auto-startup entry if it was configured
            if parsed.metadata.auto_startup {
                if let Err(e) = self.platform.configure_auto_startup(
                    &parsed.metadata.app_name,
                    Path::new(""),
                    false,
                ) {
                    warn!("Failed to remove auto-startup entry: {}", e);
                }
            }

            // Note: Uninstaller registry entry cleanup is handled separately
            // as it requires knowing the install location
        }

        if rollback_errors.is_empty() {
            info!("Rollback completed successfully");
            Ok(())
        } else {
            warn!("Rollback completed with {} errors", rollback_errors.len());
            // Still return Ok since we did our best to clean up
            Ok(())
        }
    }

    /// Rollback with registry cleanup for a specific installation.
    ///
    /// This is a more complete rollback that also removes the uninstaller
    /// registry entry.
    ///
    /// # Arguments
    /// * `installed_files` - List of files that were installed
    /// * `install_dir` - The installation directory
    pub fn rollback_with_registry(
        &self,
        installed_files: &[PathBuf],
        install_dir: &Path,
    ) -> Result<()> {
        // First do the standard rollback
        self.rollback(installed_files)?;

        // Then clean up the uninstaller registry entry
        if let Ok(parsed) = self.parse_package() {
            let uninstall_key_path = format!(
                "HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\{}",
                parsed.metadata.app_name
            );

            // Try to delete the entire uninstall key
            // Note: This requires admin privileges
            if let Err(e) = self.platform.delete_registry(&uninstall_key_path, "") {
                warn!("Failed to remove uninstaller registry key: {}", e);
            }
        }

        // Remove the install directory if it's empty
        if install_dir.exists() {
            if let Ok(mut entries) = install_dir.read_dir() {
                if entries.next().is_none() {
                    if let Err(e) = std::fs::remove_dir(install_dir) {
                        warn!("Failed to remove install directory: {}", e);
                    }
                }
            }
        }

        Ok(())
    }

    /// Create an uninstaller in the installation directory.
    ///
    /// Creates the uninstaller by:
    /// 1. Copying the current installer executable as uninstall.exe
    /// 2. Creating install.manifest.json with the list of installed files
    /// 3. Writing uninstall information to the Windows registry
    ///
    /// # Arguments
    /// * `install_dir` - The installation directory
    ///
    /// # Requirements
    /// - 11.1: Create uninstaller executable in install directory
    /// - 11.2: Create install.manifest.json with installed file list
    /// - 10.1-10.7: Write uninstall info to registry
    pub fn create_uninstaller(&self, install_dir: &Path) -> Result<()> {
        info!("Creating uninstaller in {:?}", install_dir);

        let parsed = self.parse_package()?;

        // 1. Copy the installer executable as uninstall.exe
        let uninstall_exe_path = install_dir.join("uninstall.exe");
        if let Ok(current_exe) = std::env::current_exe() {
            if current_exe.exists() {
                match std::fs::copy(&current_exe, &uninstall_exe_path) {
                    Ok(_) => {
                        info!("Copied installer to {:?}", uninstall_exe_path);
                    }
                    Err(e) => {
                        warn!("Failed to copy installer as uninstaller: {}", e);
                        // Continue anyway - manifest is more important
                    }
                }
            }
        }

        // 2. Create install.manifest.json with file list
        let manifest_path = install_dir.join("install.manifest.json");

        // Collect all installed files with their full paths
        let installed_files: Vec<String> =
            parsed.toc.files.iter().map(|f| f.path.clone()).collect();

        // Collect directories that were created
        let mut directories: Vec<String> = parsed
            .toc
            .files
            .iter()
            .filter_map(|f| {
                Path::new(&f.path)
                    .parent()
                    .map(|p| p.to_string_lossy().to_string())
            })
            .collect();
        directories.sort();
        directories.dedup();

        let manifest = serde_json::json!({
            "app_name": parsed.metadata.app_name,
            "version": parsed.metadata.version,
            "install_dir": install_dir.to_string_lossy(),
            "files": installed_files,
            "directories": directories,
            "registry_entries": parsed.metadata.registry_entries.iter().map(|e| {
                serde_json::json!({
                    "path": e.path,
                    "key": e.key
                })
            }).collect::<Vec<_>>(),
            "auto_startup": parsed.metadata.auto_startup,
            "desktop_icons": parsed.metadata.desktop_icons,
            "created_at": chrono::Utc::now().to_rfc3339(),
        });

        let manifest_data = serde_json::to_string_pretty(&manifest)
            .map_err(|e| InstallerError::Serialization(e.to_string()))?;

        write_file(&manifest_path, manifest_data.as_bytes())?;
        info!("Created uninstaller manifest at {:?}", manifest_path);

        // 3. Register uninstaller in Windows registry
        let uninstall_info = UninstallInfo {
            app_name: parsed.metadata.app_name.clone(),
            version: parsed.metadata.version.clone(),
            install_location: install_dir.to_path_buf(),
            uninstall_exe: uninstall_exe_path,
            publisher: parsed.metadata.vendor.clone(),
            estimated_size_kb: parsed
                .toc
                .files
                .iter()
                .map(|f| f.original_size)
                .sum::<u64>()
                / 1024,
        };

        if let Err(e) = self.platform.register_uninstaller(&uninstall_info) {
            warn!("Failed to register uninstaller in registry: {}", e);
            // Don't fail - the manifest is the important part
        }

        Ok(())
    }

    /// Get the package path.
    pub fn package_path(&self) -> &Path {
        &self.package_path
    }

    /// Get a reference to the platform abstraction.
    pub fn platform(&self) -> &dyn Platform {
        self.platform.as_ref()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::packager::Packager;
    use installer_shared::PackagerConfig;
    use std::fs;
    use tempfile::tempdir;

    fn create_test_ui_directory(dir: &Path) {
        fs::write(dir.join("index.html"), "<html><body>Test</body></html>").unwrap();
        let locales_dir = dir.join("locales");
        fs::create_dir_all(&locales_dir).unwrap();
        fs::write(locales_dir.join("en-US.json"), r#"{"key": "value"}"#).unwrap();
    }

    #[test]
    fn test_header_size() {
        // Verify the header size is what we expect (no padding with reordered fields)
        let header_size = std::mem::size_of::<installer_shared::PackageHeader>();
        // 8*8 + 4 + 4 + 4 + 4 + 8 = 88
        assert_eq!(header_size, 88);
    }

    #[test]
    fn test_extract_ui_resources() {
        let input_dir = tempdir().unwrap();
        let output_dir = tempdir().unwrap();
        let ui_dir = tempdir().unwrap();
        let extract_dir = tempdir().unwrap();

        // Create input file
        fs::write(input_dir.path().join("test.txt"), b"Hello, World!").unwrap();

        // Create UI resources
        create_test_ui_directory(ui_dir.path());

        let output_path = output_dir.path().join("test.pkg");

        // Build package with UI resources
        let config = PackagerConfig::default();
        let packager = Packager::new(config).unwrap();
        packager
            .build_package(input_dir.path(), &output_path, Some(ui_dir.path()), |_| {})
            .unwrap();

        // First verify we can parse the package
        let installer = Installer::new(output_path.clone()).unwrap();
        let parsed = installer.parse_package().unwrap();

        // Verify UI resources info
        assert!(parsed.has_ui_resources);
        assert!(parsed.ui_resources_size > 0);
        assert!(parsed.metadata.ui_resources_checksum.is_some());

        // Extract UI resources
        let ui_resources = installer.extract_ui_resources(extract_dir.path()).unwrap();

        assert!(ui_resources.is_some());
        let ui = ui_resources.unwrap();
        assert!(ui.locales.contains(&"en-US".to_string()));

        // Verify extracted files
        assert!(extract_dir.path().join("index.html").exists());
        assert!(extract_dir.path().join("locales/en-US.json").exists());
    }

    #[test]
    fn test_extract_ui_resources_none() {
        let input_dir = tempdir().unwrap();
        let output_dir = tempdir().unwrap();
        let extract_dir = tempdir().unwrap();

        // Create input file
        fs::write(input_dir.path().join("test.txt"), b"Hello, World!").unwrap();

        let output_path = output_dir.path().join("test.pkg");

        // Build package WITHOUT UI resources
        let config = PackagerConfig::default();
        let packager = Packager::new(config).unwrap();
        packager
            .build_package(input_dir.path(), &output_path, None, |_| {})
            .unwrap();

        // Try to extract UI resources
        let installer = Installer::new(output_path).unwrap();
        let ui_resources = installer.extract_ui_resources(extract_dir.path()).unwrap();

        assert!(ui_resources.is_none());
    }

    #[test]
    fn test_has_ui_resources() {
        let input_dir = tempdir().unwrap();
        let output_dir = tempdir().unwrap();
        let ui_dir = tempdir().unwrap();

        // Create input file
        fs::write(input_dir.path().join("test.txt"), b"Hello, World!").unwrap();

        // Create UI resources
        create_test_ui_directory(ui_dir.path());

        let output_path = output_dir.path().join("test.pkg");

        // Build package with UI resources
        let config = PackagerConfig::default();
        let packager = Packager::new(config).unwrap();
        packager
            .build_package(input_dir.path(), &output_path, Some(ui_dir.path()), |_| {})
            .unwrap();

        let installer = Installer::new(output_path).unwrap();
        assert!(installer.has_ui_resources().unwrap());
    }

    #[test]
    fn test_default_flow_parse_error_has_context() {
        let invalid_yaml = r#"
version: 1
install_flow:
  steps:
    - id: broken_script
      type: script
      params:
        path: scripts/broken.js
"#;

        let err =
            Installer::parse_default_install_flow_yaml(invalid_yaml).expect_err("should fail");
        match err {
            InstallerError::Config(msg) => {
                assert!(msg.contains("built-in default install flow YAML"));
                assert!(msg.contains("broken_script"));
            }
            other => panic!("unexpected error: {other}"),
        }
    }
}

// ============================================================================
// Property-Based Tests
// ============================================================================

#[cfg(test)]
mod property_tests {
    use super::*;
    use crate::packager::Packager;
    use installer_shared::PackagerConfig;
    use proptest::prelude::*;
    use std::fs;
    use tempfile::tempdir;

    /// Generate random file content of varying sizes
    fn file_content_strategy() -> impl Strategy<Value = Vec<u8>> {
        prop::collection::vec(any::<u8>(), 100..5000)
    }

    /// Generate a valid filename
    fn filename_strategy() -> impl Strategy<Value = String> {
        "[a-zA-Z][a-zA-Z0-9_]{1,15}\\.(txt|bin|dat)"
    }

    /// Generate a list of files with content
    fn files_strategy() -> impl Strategy<Value = Vec<(String, Vec<u8>)>> {
        prop::collection::vec((filename_strategy(), file_content_strategy()), 1..10)
    }

    proptest! {
        #![proptest_config(ProptestConfig::with_cases(20))]

        /// **Property 6: Parallel Decompression Order Preservation**
        /// For any package, parallel decompression should produce the same file
        /// content and order as sequential decompression.
        ///
        /// **Validates: Requirements 3.9, 13.6**
        #[test]
        fn prop_parallel_decompression_preserves_order(
            files in files_strategy()
        ) {
            let input_dir = tempdir().expect("Failed to create input dir");
            let output_dir = tempdir().expect("Failed to create output dir");
            let install_dir = tempdir().expect("Failed to create install dir");

            // Create unique filenames to avoid collisions
            let mut created_files: Vec<(String, Vec<u8>)> = Vec::new();
            for (idx, (name, content)) in files.iter().enumerate() {
                let unique_name = format!("{}_{}", idx, name);
                let file_path = input_dir.path().join(&unique_name);
                fs::write(&file_path, content).expect("Failed to write file");
                created_files.push((unique_name, content.clone()));
            }

            let output_path = output_dir.path().join("test.pkg");

            // Build package
            let config = PackagerConfig::default();
            let packager = Packager::new(config).expect("Failed to create packager");
            packager
                .build_package(input_dir.path(), &output_path, None, |_| {})
                .expect("Failed to build package");

            // Install with parallel decompression
            let installer = Installer::new(output_path).expect("Failed to create installer");
            let options = InstallOptions {
                install_dir: install_dir.path().to_path_buf(),
                create_shortcuts: false,
                configure_registry: false,
                auto_startup: false,
                components: std::collections::BTreeMap::new(),
                silent: true,
                thread_count: Some(4), // Force multiple threads
            };

            let stats = installer.install(options, |_| {}).expect("Installation failed");

            // Verify all files were installed
            prop_assert_eq!(stats.installed_files, created_files.len(),
                "Number of installed files should match");

            // Verify file contents match original
            for (name, original_content) in &created_files {
                let installed_path = install_dir.path().join(name);
                prop_assert!(installed_path.exists(),
                    "File {} should exist after installation", name);

                let installed_content = fs::read(&installed_path)
                    .expect("Failed to read installed file");
                prop_assert_eq!(&installed_content, original_content,
                    "Content of {} should match original", name);
            }
        }

        /// **Property 6 (continued): Parallel vs Sequential Consistency**
        /// Multiple installations of the same package should produce identical results.
        ///
        /// **Validates: Requirements 3.9, 13.6**
        #[test]
        fn prop_parallel_decompression_deterministic(
            files in files_strategy()
        ) {
            let input_dir = tempdir().expect("Failed to create input dir");
            let output_dir = tempdir().expect("Failed to create output dir");
            let install_dir1 = tempdir().expect("Failed to create install dir 1");
            let install_dir2 = tempdir().expect("Failed to create install dir 2");

            // Create files
            let mut created_files: Vec<String> = Vec::new();
            for (idx, (name, content)) in files.iter().enumerate() {
                let unique_name = format!("{}_{}", idx, name);
                let file_path = input_dir.path().join(&unique_name);
                fs::write(&file_path, content).expect("Failed to write file");
                created_files.push(unique_name);
            }

            let output_path = output_dir.path().join("test.pkg");

            // Build package
            let config = PackagerConfig::default();
            let packager = Packager::new(config).expect("Failed to create packager");
            packager
                .build_package(input_dir.path(), &output_path, None, |_| {})
                .expect("Failed to build package");

            // Install twice with different thread counts
            let installer = Installer::new(output_path).expect("Failed to create installer");

            let options1 = InstallOptions {
                install_dir: install_dir1.path().to_path_buf(),
                create_shortcuts: false,
                configure_registry: false,
                auto_startup: false,
                components: std::collections::BTreeMap::new(),
                silent: true,
                thread_count: Some(1), // Single thread
            };

            let options2 = InstallOptions {
                install_dir: install_dir2.path().to_path_buf(),
                create_shortcuts: false,
                configure_registry: false,
                auto_startup: false,
                components: std::collections::BTreeMap::new(),
                silent: true,
                thread_count: Some(4), // Multiple threads
            };

            installer.install(options1, |_| {}).expect("Installation 1 failed");

            // Need to recreate installer since it consumes the package
            let installer2 = Installer::new(output_dir.path().join("test.pkg"))
                .expect("Failed to create installer 2");
            installer2.install(options2, |_| {}).expect("Installation 2 failed");

            // Compare installed files
            for name in &created_files {
                let path1 = install_dir1.path().join(name);
                let path2 = install_dir2.path().join(name);

                let content1 = fs::read(&path1).expect("Failed to read from install 1");
                let content2 = fs::read(&path2).expect("Failed to read from install 2");

                prop_assert_eq!(content1, content2,
                    "File {} should have identical content in both installations", name);
            }
        }
    }
}

// ============================================================================
// Property-Based Tests for Rollback Integrity
// ============================================================================

#[cfg(test)]
mod rollback_property_tests {
    use super::*;
    use crate::packager::Packager;
    use installer_shared::PackagerConfig;
    use proptest::prelude::*;
    use std::fs;
    use tempfile::tempdir;

    /// Generate random file content
    fn file_content_strategy() -> impl Strategy<Value = Vec<u8>> {
        prop::collection::vec(any::<u8>(), 50..2000)
    }

    /// Generate a valid filename
    fn filename_strategy() -> impl Strategy<Value = String> {
        "[a-zA-Z][a-zA-Z0-9_]{1,10}\\.(txt|bin)"
    }

    /// Generate a list of files with content
    fn files_strategy() -> impl Strategy<Value = Vec<(String, Vec<u8>)>> {
        prop::collection::vec((filename_strategy(), file_content_strategy()), 1..8)
    }

    proptest! {
        #![proptest_config(ProptestConfig::with_cases(20))]

        /// **Property 8: Installation Rollback Integrity**
        /// For any installation process, if aborted mid-way, all written files
        /// and created directories should be completely cleaned up.
        ///
        /// **Validates: Requirements 8.5, 8.6**
        #[test]
        fn prop_rollback_cleans_all_files(
            files in files_strategy()
        ) {
            let input_dir = tempdir().expect("Failed to create input dir");
            let output_dir = tempdir().expect("Failed to create output dir");
            let install_dir = tempdir().expect("Failed to create install dir");

            // Create files
            let mut created_files: Vec<PathBuf> = Vec::new();
            for (idx, (name, content)) in files.iter().enumerate() {
                let unique_name = format!("{}_{}", idx, name);
                let file_path = input_dir.path().join(&unique_name);
                fs::write(&file_path, content).expect("Failed to write file");
            }

            let output_path = output_dir.path().join("test.pkg");

            // Build package
            let config = PackagerConfig::default();
            let packager = Packager::new(config).expect("Failed to create packager");
            packager
                .build_package(input_dir.path(), &output_path, None, |_| {})
                .expect("Failed to build package");

            // Install
            let installer = Installer::new(output_path).expect("Failed to create installer");
            let options = InstallOptions {
                install_dir: install_dir.path().to_path_buf(),
                create_shortcuts: false,
                configure_registry: false,
                auto_startup: false,
                components: std::collections::BTreeMap::new(),
                silent: true,
                thread_count: None,
            };

            installer.install(options, |_| {}).expect("Installation failed");

            // Collect installed files
            for entry in walkdir::WalkDir::new(install_dir.path())
                .into_iter()
                .filter_map(|e| e.ok())
                .filter(|e| e.file_type().is_file())
            {
                created_files.push(entry.path().to_path_buf());
            }

            // Verify files were installed
            prop_assert!(!created_files.is_empty(), "Some files should be installed");

            // Perform rollback
            installer.rollback(&created_files).expect("Rollback failed");

            // Verify all files are deleted
            for file in &created_files {
                prop_assert!(!file.exists(),
                    "File {:?} should be deleted after rollback", file);
            }

            // Verify install directory is empty or contains only empty subdirs
            let remaining_files: Vec<_> = walkdir::WalkDir::new(install_dir.path())
                .into_iter()
                .filter_map(|e| e.ok())
                .filter(|e| e.file_type().is_file())
                .collect();

            prop_assert!(remaining_files.is_empty(),
                "No files should remain after rollback, found: {:?}", remaining_files);
        }

        /// **Property 8 (continued): Rollback Removes Empty Directories**
        /// After rollback, all directories created during installation should be
        /// removed if they are empty.
        ///
        /// **Validates: Requirements 8.5, 8.6**
        #[test]
        fn prop_rollback_removes_empty_directories(
            files in files_strategy()
        ) {
            let input_dir = tempdir().expect("Failed to create input dir");
            let output_dir = tempdir().expect("Failed to create output dir");
            let install_dir = tempdir().expect("Failed to create install dir");

            // Create files in subdirectories
            let subdir = input_dir.path().join("subdir");
            fs::create_dir_all(&subdir).expect("Failed to create subdir");

            let mut created_files: Vec<PathBuf> = Vec::new();
            for (idx, (name, content)) in files.iter().enumerate() {
                let unique_name = format!("{}_{}", idx, name);
                let file_path = subdir.join(&unique_name);
                fs::write(&file_path, content).expect("Failed to write file");
            }

            let output_path = output_dir.path().join("test.pkg");

            // Build package
            let config = PackagerConfig::default();
            let packager = Packager::new(config).expect("Failed to create packager");
            packager
                .build_package(input_dir.path(), &output_path, None, |_| {})
                .expect("Failed to build package");

            // Install
            let installer = Installer::new(output_path).expect("Failed to create installer");
            let options = InstallOptions {
                install_dir: install_dir.path().to_path_buf(),
                create_shortcuts: false,
                configure_registry: false,
                auto_startup: false,
                components: std::collections::BTreeMap::new(),
                silent: true,
                thread_count: None,
            };

            installer.install(options, |_| {}).expect("Installation failed");

            // Collect installed files
            for entry in walkdir::WalkDir::new(install_dir.path())
                .into_iter()
                .filter_map(|e| e.ok())
                .filter(|e| e.file_type().is_file())
            {
                created_files.push(entry.path().to_path_buf());
            }

            // Verify subdir was created
            let installed_subdir = install_dir.path().join("subdir");
            prop_assert!(installed_subdir.exists(),
                "Subdirectory should exist after installation");

            // Perform rollback
            installer.rollback(&created_files).expect("Rollback failed");

            // Verify subdirectory is removed (since it should be empty)
            // Note: The rollback removes empty directories, so if all files
            // in subdir are deleted, the subdir itself should be removed
            let remaining_dirs: Vec<_> = walkdir::WalkDir::new(install_dir.path())
                .into_iter()
                .filter_map(|e| e.ok())
                .filter(|e| e.file_type().is_dir() && e.path() != install_dir.path())
                .collect();

            // All subdirectories should be empty or removed
            for dir_entry in &remaining_dirs {
                let dir_path = dir_entry.path();
                if dir_path.exists() {
                    let is_empty = dir_path.read_dir()
                        .map(|mut d| d.next().is_none())
                        .unwrap_or(false);
                    // Empty directories are acceptable (they'll be cleaned up)
                    // but they shouldn't contain any files
                    if !is_empty {
                        let contents: Vec<_> = dir_path.read_dir()
                            .unwrap()
                            .filter_map(|e| e.ok())
                            .collect();
                        prop_assert!(contents.iter().all(|e| e.file_type().map(|t| t.is_dir()).unwrap_or(false)),
                            "Directory {:?} should only contain empty subdirs, found: {:?}", dir_path, contents);
                    }
                }
            }
        }
    }
}
