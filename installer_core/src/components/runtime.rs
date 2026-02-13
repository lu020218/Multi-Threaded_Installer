use std::collections::HashMap;
use std::path::PathBuf;

use chrono::Utc;

use installer_shared::Result;

use crate::components::ComponentManifest;
use crate::filesystem::create_dir_all;

#[derive(Debug, Clone)]
pub struct InstalledComponentRecord {
    pub component_id: String,
    pub created_paths: Vec<PathBuf>,
    pub rollback_paths: Vec<PathBuf>,
    pub uninstall_product_code: Option<String>,
}

#[derive(Debug, Default)]
pub struct ComponentRuntimeState {
    pub manifest: Option<ComponentManifest>,
    pub downloaded_files: HashMap<String, PathBuf>,
    pub selected_components: Vec<String>,
    pub cache_root: Option<PathBuf>,
    pub install_root: PathBuf,
    pub installed_components: Vec<InstalledComponentRecord>,
}

impl ComponentRuntimeState {
    pub fn new(install_root: PathBuf) -> Self {
        Self {
            install_root,
            ..Default::default()
        }
    }

    pub fn cache_root(&mut self) -> Result<PathBuf> {
        if let Some(root) = &self.cache_root {
            return Ok(root.clone());
        }

        let mut root = std::env::temp_dir();
        root.push(format!(
            "mti_component_cache_{}_{}",
            std::process::id(),
            Utc::now().timestamp_nanos_opt().unwrap_or_default()
        ));
        create_dir_all(&root)?;
        self.cache_root = Some(root.clone());
        Ok(root)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn cache_root_is_stable_after_first_creation() {
        let install_root = std::env::temp_dir().join("mti_test_install_root");
        let mut state = ComponentRuntimeState::new(install_root);

        let first = state.cache_root().expect("cache root");
        let second = state.cache_root().expect("cache root again");

        assert_eq!(first, second);
        assert!(first.exists());
    }
}
