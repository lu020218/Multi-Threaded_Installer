use installer_core::Installer;
use serde::Serialize;
use std::path::{Path, PathBuf};
use tempfile::TempDir;
use tracing::{error, info, warn};

/// Extracted custom UI resources and owning temp dir.
pub struct CustomUiResources {
    pub dir: PathBuf,
    _temp_dir: TempDir,
}

/// Custom UI content structure.
#[derive(Serialize)]
pub struct CustomUIContent {
    pub html: String,
    pub css: Option<String>,
    pub js: Option<String>,
}

/// Extract custom UI resources from the embedded package.
pub fn extract_custom_ui_resources(package_path: &Path) -> Option<CustomUiResources> {
    info!(
        "Checking for custom UI resources in package: {:?}",
        package_path
    );

    let installer = match Installer::new(package_path.to_path_buf()) {
        Ok(i) => i,
        Err(e) => {
            warn!("Failed to open package for UI extraction: {}", e);
            return None;
        }
    };

    let has_ui = match installer.has_ui_resources() {
        Ok(has) => has,
        Err(e) => {
            warn!("Failed to check UI resources: {}", e);
            return None;
        }
    };

    if !has_ui {
        info!("Package does not contain custom UI resources, using default UI");
        return None;
    }

    let temp_dir = match tempfile::tempdir() {
        Ok(dir) => dir,
        Err(e) => {
            error!("Failed to create temp directory for UI: {}", e);
            return None;
        }
    };

    let ui_dir = temp_dir.path().to_path_buf();
    match installer.extract_ui_resources(&ui_dir) {
        Ok(Some(_)) => {
            info!("Extracted custom UI resources to: {:?}", ui_dir);
            Some(CustomUiResources {
                dir: ui_dir,
                _temp_dir: temp_dir,
            })
        }
        Ok(None) => {
            info!("No UI resources to extract");
            None
        }
        Err(e) => {
            error!("Failed to extract UI resources: {}", e);
            None
        }
    }
}

fn load_custom_ui_content_from_dir(ui_path: &Path) -> Option<CustomUIContent> {
    if !ui_path.exists() {
        return None;
    }

    let html_path = ui_path.join("index.html");
    let html = std::fs::read_to_string(&html_path).ok()?;

    let css_path = ui_path.join("styles").join("main.css");
    let css = std::fs::read_to_string(&css_path).ok();

    let js_path = ui_path.join("scripts").join("main.js");
    let js = std::fs::read_to_string(&js_path).ok();

    Some(CustomUIContent { html, css, js })
}

#[tauri::command]
pub fn get_custom_ui_content() -> Option<CustomUIContent> {
    let ui_dir = std::env::var("INSTALLER_CUSTOM_UI_DIR").ok()?;
    let ui_path = Path::new(&ui_dir);

    let content = load_custom_ui_content_from_dir(ui_path)?;
    info!(
        "Loaded custom UI: html={} bytes, css={:?} bytes, js={:?} bytes",
        content.html.len(),
        content.css.as_ref().map(|s| s.len()),
        content.js.as_ref().map(|s| s.len())
    );
    Some(content)
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::fs;
    use tempfile::tempdir;

    #[test]
    fn returns_none_when_directory_missing() {
        let dir = tempdir().expect("tempdir");
        let missing = dir.path().join("missing");
        assert!(load_custom_ui_content_from_dir(&missing).is_none());
    }

    #[test]
    fn returns_none_when_index_missing() {
        let dir = tempdir().expect("tempdir");
        fs::create_dir_all(dir.path().join("styles")).expect("styles dir");
        fs::write(dir.path().join("styles/main.css"), "body{}").expect("write css");

        assert!(load_custom_ui_content_from_dir(dir.path()).is_none());
    }

    #[test]
    fn css_js_are_optional() {
        let dir = tempdir().expect("tempdir");
        fs::write(dir.path().join("index.html"), "<html></html>").expect("write html");

        let content = load_custom_ui_content_from_dir(dir.path()).expect("content");
        assert_eq!(content.html, "<html></html>");
        assert!(content.css.is_none());
        assert!(content.js.is_none());
    }

    #[test]
    fn loads_html_css_js_when_present() {
        let dir = tempdir().expect("tempdir");
        fs::create_dir_all(dir.path().join("styles")).expect("styles dir");
        fs::create_dir_all(dir.path().join("scripts")).expect("scripts dir");

        fs::write(dir.path().join("index.html"), "<html>ok</html>").expect("write html");
        fs::write(dir.path().join("styles/main.css"), "body{color:red;}").expect("write css");
        fs::write(dir.path().join("scripts/main.js"), "console.log('ok')").expect("write js");

        let content = load_custom_ui_content_from_dir(dir.path()).expect("content");
        assert_eq!(content.html, "<html>ok</html>");
        assert_eq!(content.css.as_deref(), Some("body{color:red;}"));
        assert_eq!(content.js.as_deref(), Some("console.log('ok')"));
    }
}
