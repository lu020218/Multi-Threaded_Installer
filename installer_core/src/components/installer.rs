use std::path::{Path, PathBuf};

use serde_json::Value;

use installer_shared::{FlowStep, InstallerError, Result};

use crate::components::runtime::{ComponentRuntimeState, InstalledComponentRecord};
use crate::components::{
    download_component_to_cache, find_component, load_component_manifest, verify_component_sha256,
    verify_component_signature, ComponentDownloadPolicy, ComponentEntry, ComponentSignaturePolicy,
};
use crate::flow_executor::FlowContext;
use crate::flow_steps::{
    STEP_DOWNLOAD_COMPONENT, STEP_INSTALL_COMPONENT, STEP_PROCESS_SELECTED_COMPONENTS,
    STEP_ROLLBACK_COMPONENT, STEP_VERIFY_COMPONENT,
};
use crate::installer::Installer;
use crate::process::{run_program, split_command_args};
use crate::step_params::StepParams;
use tracing::debug;

const INSTALL_KIND_ARCHIVE: &str = "archive";
const INSTALL_KIND_MSI: &str = "msi";
const INSTALL_KIND_EXE: &str = "exe";
const PROGRAM_MSIEXEC: &str = "msiexec";

pub fn load_component_manifest_step(
    installer: &Installer,
    step: &FlowStep,
    state: &mut ComponentRuntimeState,
) -> Result<()> {
    let params = StepParams::from_step(step);
    let path = params.required_str_for_step("path", "load_component_manifest")?;

    let manifest = if path == "embedded" {
        let parsed = installer.parse_package()?;
        let yaml = parsed.metadata.embedded_component_manifest.ok_or_else(|| {
            InstallerError::Config(
                "Embedded component manifest not found in package metadata".to_string(),
            )
        })?;
        serde_yaml::from_str(&yaml).map_err(|e| {
            InstallerError::Config(format!(
                "Failed to parse embedded component manifest: {}",
                e
            ))
        })?
    } else {
        load_component_manifest(Path::new(path))?
    };

    state.manifest = Some(manifest);
    Ok(())
}

pub fn download_component_step(
    step: &FlowStep,
    state: &mut ComponentRuntimeState,
    context: &FlowContext,
    policy: &ComponentDownloadPolicy,
) -> Result<()> {
    let component_ids = resolve_component_ids(step, state, context, STEP_DOWNLOAD_COMPONENT)?;
    let cache_root = state.cache_root()?;
    for component_id in component_ids {
        download_component_by_id(state, &cache_root, &component_id, policy)?;
    }
    Ok(())
}

pub fn resolve_selected_components_step(
    step: &FlowStep,
    state: &mut ComponentRuntimeState,
    context: &mut FlowContext,
) -> Result<()> {
    let params = StepParams::from_step(step);
    let include_required = params.bool_or("include_required", true);

    let manifest = state.manifest.as_ref().ok_or_else(|| {
        InstallerError::Config(
            "resolve_selected_components requires load_component_manifest first".to_string(),
        )
    })?;

    let mut selected = Vec::new();
    if let Some(component_opts) = context.options.get("components").and_then(Value::as_object) {
        for component in &manifest.components {
            if component_opts
                .get(component.id.as_str())
                .and_then(Value::as_bool)
                .unwrap_or(false)
            {
                selected.push(component.id.clone());
            }
        }
    }

    if include_required {
        for component in &manifest.components {
            if component.required && !selected.iter().any(|id| id == &component.id) {
                selected.push(component.id.clone());
            }
        }
    }

    context.set_var(
        "SelectedComponents",
        Value::Array(
            selected
                .iter()
                .cloned()
                .map(Value::String)
                .collect::<Vec<_>>(),
        ),
    );
    state.selected_components = selected;
    Ok(())
}

pub fn process_selected_components_step(
    step: &FlowStep,
    state: &mut ComponentRuntimeState,
    context: &FlowContext,
    download_policy: &ComponentDownloadPolicy,
    signature_policy: &ComponentSignaturePolicy,
    sanitize_relative_path: fn(&str) -> Result<PathBuf>,
) -> Result<()> {
    let params = StepParams::from_step(step);
    let action = params
        .required_str_for_step("action", "process_selected_components")?
        .to_ascii_lowercase();

    let component_ids =
        resolve_component_ids(step, state, context, STEP_PROCESS_SELECTED_COMPONENTS)?;
    let cache_root = if action == "download" {
        Some(state.cache_root()?)
    } else {
        None
    };

    for component_id in component_ids {
        match action.as_str() {
            "download" => download_component_by_id(
                state,
                cache_root.as_ref().expect("cache root must be initialized"),
                &component_id,
                download_policy,
            )?,
            "verify" => verify_component_by_id(state, &component_id, signature_policy)?,
            "install" => {
                install_component_by_id(step, state, &component_id, sanitize_relative_path)?
            }
            other => {
                return Err(InstallerError::Config(format!(
                    "Unsupported action '{}' for process_selected_components, expected download/verify/install",
                    other
                )))
            }
        }
    }

    Ok(())
}

pub fn verify_component_step(
    step: &FlowStep,
    state: &mut ComponentRuntimeState,
    context: &FlowContext,
    signature_policy: &ComponentSignaturePolicy,
) -> Result<()> {
    let component_ids = resolve_component_ids(step, state, context, STEP_VERIFY_COMPONENT)?;
    for component_id in component_ids {
        verify_component_by_id(state, &component_id, signature_policy)?;
    }
    Ok(())
}

pub fn install_component_step(
    step: &FlowStep,
    state: &mut ComponentRuntimeState,
    context: &FlowContext,
    sanitize_relative_path: fn(&str) -> Result<PathBuf>,
) -> Result<()> {
    let component_ids = resolve_component_ids(step, state, context, STEP_INSTALL_COMPONENT)?;
    for component_id in component_ids {
        install_component_by_id(step, state, &component_id, sanitize_relative_path)?;
    }
    Ok(())
}

pub fn rollback_component_step(state: &mut ComponentRuntimeState) -> Result<()> {
    for record in state.installed_components.iter().rev() {
        debug!("Rolling back component '{}'", record.component_id);

        if let Some(product_code) = record.uninstall_product_code.as_deref() {
            let args = vec![
                "/x".to_string(),
                product_code.to_string(),
                "/qn".to_string(),
                "/norestart".to_string(),
            ];
            run_program(PROGRAM_MSIEXEC, &args, STEP_ROLLBACK_COMPONENT)?;
        }

        for path in record.rollback_paths.iter().rev() {
            if !path.exists() {
                continue;
            }
            remove_component_path(path).map_err(|e| {
                InstallerError::Rollback(format!(
                    "Failed to remove component rollback path '{}': {}",
                    path.display(),
                    e
                ))
            })?;
        }

        for path in record.created_paths.iter().rev() {
            if !path.exists() {
                continue;
            }
            remove_component_path(path).map_err(|e| {
                InstallerError::Rollback(format!(
                    "Failed to remove component path '{}': {}",
                    path.display(),
                    e
                ))
            })?;
        }
    }

    state.installed_components.clear();
    Ok(())
}

fn download_component_by_id(
    state: &mut ComponentRuntimeState,
    cache_root: &Path,
    component_id: &str,
    policy: &ComponentDownloadPolicy,
) -> Result<()> {
    let component = {
        let manifest = state.manifest.as_ref().ok_or_else(|| {
            InstallerError::Config(
                "download_component requires load_component_manifest first".to_string(),
            )
        })?;
        find_component(manifest, component_id)?.clone()
    };

    let downloaded = download_component_to_cache(&component, cache_root, policy)?;
    state
        .downloaded_files
        .insert(component_id.to_string(), downloaded);
    Ok(())
}

fn verify_component_by_id(
    state: &ComponentRuntimeState,
    component_id: &str,
    signature_policy: &ComponentSignaturePolicy,
) -> Result<()> {
    let manifest = state.manifest.as_ref().ok_or_else(|| {
        InstallerError::Config(
            "verify_component requires load_component_manifest first".to_string(),
        )
    })?;
    let component = find_component(manifest, component_id)?;
    let downloaded = state.downloaded_files.get(component_id).ok_or_else(|| {
        InstallerError::Config(format!(
            "verify_component requires download_component first for '{}'",
            component_id
        ))
    })?;

    verify_component_sha256(downloaded, &component.package.sha256)?;
    verify_component_signature(manifest, component, signature_policy)?;
    Ok(())
}

fn install_component_by_id(
    step: &FlowStep,
    state: &mut ComponentRuntimeState,
    component_id: &str,
    sanitize_relative_path: fn(&str) -> Result<PathBuf>,
) -> Result<()> {
    let component = {
        let manifest = state.manifest.as_ref().ok_or_else(|| {
            InstallerError::Config(
                "install_component requires load_component_manifest first".to_string(),
            )
        })?;
        find_component(manifest, component_id)?.clone()
    };

    let downloaded = state
        .downloaded_files
        .get(component_id)
        .cloned()
        .ok_or_else(|| {
            InstallerError::Config(format!(
                "install_component requires download_component first for '{}'",
                component_id
            ))
        })?;

    install_single_component(
        step,
        state,
        component_id.to_string(),
        &component,
        &downloaded,
        sanitize_relative_path,
    )
}

fn install_single_component(
    step: &FlowStep,
    state: &mut ComponentRuntimeState,
    component_id: String,
    component: &ComponentEntry,
    downloaded: &Path,
    sanitize_relative_path: fn(&str) -> Result<PathBuf>,
) -> Result<()> {
    let install_spec = component.install.as_ref().ok_or_else(|| {
        InstallerError::Config(format!(
            "Component '{}' missing install specification",
            component_id
        ))
    })?;

    let mut created_paths = Vec::new();
    let mut rollback_paths = Vec::new();

    if let Some(rollback) = component.rollback.as_ref() {
        for raw in &rollback.remove_paths {
            let relative = sanitize_relative_path(raw)?;
            rollback_paths.push(state.install_root.join(relative));
        }
    }

    match install_spec.kind.to_ascii_lowercase().as_str() {
        INSTALL_KIND_ARCHIVE => {
            let target_subdir = install_spec
                .target_subdir
                .as_deref()
                .map(sanitize_relative_path)
                .transpose()?
                .unwrap_or_else(|| PathBuf::from(format!("components/{}", component_id)));
            let target_dir = state.install_root.join(target_subdir);
            std::fs::create_dir_all(&target_dir)?;

            let lower_name = downloaded
                .file_name()
                .and_then(|s| s.to_str())
                .unwrap_or("")
                .to_ascii_lowercase();

            if lower_name.ends_with(".tar.gz")
                || lower_name.ends_with(".tgz")
                || lower_name.ends_with(".tar")
            {
                extract_component_archive(downloaded, &target_dir)?;
                created_paths.push(target_dir.clone());
            } else {
                let file_name = downloaded
                    .file_name()
                    .and_then(|s| s.to_str())
                    .unwrap_or("component.bin");
                let target_file = target_dir.join(file_name);
                std::fs::copy(downloaded, &target_file)?;
                created_paths.push(target_file);
                created_paths.push(target_dir.clone());
            }

            if !rollback_paths.iter().any(|p| p == &target_dir) {
                rollback_paths.push(target_dir);
            }
        }
        INSTALL_KIND_MSI => {
            let mut args = vec![
                "/i".to_string(),
                downloaded.to_string_lossy().to_string(),
                "/qn".to_string(),
                "/norestart".to_string(),
            ];
            if let Some(extra) = install_spec.args.as_deref() {
                args.extend(split_command_args(extra));
            }
            run_program(PROGRAM_MSIEXEC, &args, &step.id)?;
        }
        INSTALL_KIND_EXE => {
            let mut args = Vec::new();
            if let Some(extra) = install_spec.args.as_deref() {
                args.extend(split_command_args(extra));
            }
            let program = downloaded.to_string_lossy().to_string();
            run_program(&program, &args, &step.id)?;
        }
        other => {
            return Err(InstallerError::Config(format!(
                "Unsupported install_component kind '{}', expected archive/msi/exe",
                other
            )))
        }
    }

    state.installed_components.push(InstalledComponentRecord {
        component_id,
        created_paths,
        rollback_paths,
        uninstall_product_code: component
            .rollback
            .as_ref()
            .and_then(|r| r.uninstall_product_code.clone()),
    });

    Ok(())
}

fn extract_component_archive(archive_path: &Path, target_dir: &Path) -> Result<()> {
    use flate2::read::GzDecoder;
    use tar::Archive;

    let file = std::fs::File::open(archive_path)?;
    let lower_name = archive_path
        .file_name()
        .and_then(|s| s.to_str())
        .unwrap_or("")
        .to_ascii_lowercase();

    if lower_name.ends_with(".tar.gz") || lower_name.ends_with(".tgz") {
        let decoder = GzDecoder::new(file);
        let mut archive = Archive::new(decoder);
        for entry in archive.entries()? {
            let mut entry = entry?;
            entry.unpack_in(target_dir)?;
        }
        return Ok(());
    }

    if lower_name.ends_with(".tar") {
        let mut archive = Archive::new(file);
        for entry in archive.entries()? {
            let mut entry = entry?;
            entry.unpack_in(target_dir)?;
        }
        return Ok(());
    }

    Err(InstallerError::Config(format!(
        "Unsupported archive format '{}'",
        archive_path.display()
    )))
}

fn remove_component_path(path: &Path) -> std::io::Result<()> {
    if path.is_file() {
        std::fs::remove_file(path)
    } else if path.is_dir() {
        std::fs::remove_dir_all(path)
    } else {
        Ok(())
    }
}

fn resolve_component_ids(
    step: &FlowStep,
    state: &ComponentRuntimeState,
    context: &FlowContext,
    step_type: &str,
) -> Result<Vec<String>> {
    let params = StepParams::from_step(step);

    if let Some(component_ids) = params.string_array("component_ids") {
        return Ok(component_ids);
    }

    if let Some(component_id) = params.str("component_id") {
        return Ok(vec![component_id.to_string()]);
    }

    let from_selected = params.bool_or("from_selected", false);
    if from_selected {
        if let Some(ids) = context
            .vars
            .get("SelectedComponents")
            .and_then(Value::as_array)
            .map(|arr| {
                arr.iter()
                    .filter_map(Value::as_str)
                    .map(str::to_string)
                    .collect::<Vec<_>>()
            })
        {
            if !ids.is_empty() {
                return Ok(ids);
            }
        }
        if !state.selected_components.is_empty() {
            return Ok(state.selected_components.clone());
        }
    }

    if let Some(ids) = context
        .vars
        .get("SelectedComponents")
        .and_then(Value::as_array)
        .map(|arr| {
            arr.iter()
                .filter_map(Value::as_str)
                .map(str::to_string)
                .collect::<Vec<_>>()
        })
    {
        if !ids.is_empty() {
            return Ok(ids);
        }
    }

    if !state.selected_components.is_empty() {
        return Ok(state.selected_components.clone());
    }

    Err(InstallerError::Config(format!(
        "Step '{}' requires params.component_id/component_ids for {} (or resolved selected components)",
        step.id, step_type
    )))
}
