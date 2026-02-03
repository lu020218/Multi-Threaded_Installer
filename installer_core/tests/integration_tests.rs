//! Integration tests for the installer system.

use installer_core::{
    calculate_crc32, verify_crc32, FlowDefinition, FlowStep, InstallFlow, InstallOptions,
    Installer, OnFailPolicy, Packager, PackagerConfig, Phase, ScriptPolicy, Uninstaller,
};
use serde_json::json;
use std::fs;
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicUsize, Ordering};
use std::sync::{Arc, Mutex};
use tempfile::tempdir;

fn create_test_files(dir: &Path, files: &[(&str, &[u8])]) {
    for (name, content) in files {
        let file_path = dir.join(name);
        if let Some(parent) = file_path.parent() {
            fs::create_dir_all(parent).expect("create parent");
        }
        fs::write(&file_path, content).expect("write file");
    }
}

fn verify_installed_files(install_dir: &Path, expected_files: &[(&str, &[u8])]) -> bool {
    for (name, expected_content) in expected_files {
        let file_path = install_dir.join(name);
        if !file_path.exists() {
            return false;
        }
        let actual = fs::read(&file_path).expect("read file");
        if actual != *expected_content {
            return false;
        }
    }
    true
}

fn create_ui_resources(dir: &Path) {
    fs::write(dir.join("index.html"), "<html></html>").expect("write html");
    let locales = dir.join("locales");
    fs::create_dir_all(&locales).expect("create locales");
    fs::write(locales.join("en-US.json"), r#"{"k":"v"}"#).expect("write locale");
}

fn has_node_runtime() -> bool {
    std::process::Command::new("node")
        .arg("--version")
        .output()
        .map(|o| o.status.success())
        .unwrap_or(false)
}

// 16.1 Complete Installation Flow Tests

#[test]
fn test_complete_install_flow_basic() {
    let input_dir = tempdir().unwrap();
    let output_dir = tempdir().unwrap();
    let install_dir = tempdir().unwrap();

    let test_files: Vec<(&str, &[u8])> = vec![
        ("readme.txt", b"readme content"),
        ("app.exe", b"exe content"),
        ("data/config.json", b"{}"),
    ];
    create_test_files(input_dir.path(), &test_files);

    let output_path = output_dir.path().join("test.pkg");
    let config = PackagerConfig {
        application_name: "TestApp".to_string(),
        version: "1.0.0".to_string(),
        ..Default::default()
    };
    let packager = Packager::new(config).unwrap();

    let events = Arc::new(Mutex::new(Vec::new()));
    let events_clone = events.clone();
    let stats = packager
        .build_package(input_dir.path(), &output_path, None, move |e| {
            events_clone.lock().unwrap().push(e.phase.clone());
        })
        .unwrap();

    assert!(output_path.exists());
    assert_eq!(stats.total_files, 3);
    assert!(!events.lock().unwrap().is_empty());

    let installer = Installer::new(output_path).unwrap();
    let options = InstallOptions {
        install_dir: install_dir.path().to_path_buf(),
        create_shortcuts: false,
        configure_registry: false,
        auto_startup: false,
        components: std::collections::BTreeMap::new(),
        silent: true,
        thread_count: None,
    };
    let install_stats = installer.install(options, |_| {}).unwrap();

    assert_eq!(install_stats.installed_files, 3);
    assert!(verify_installed_files(install_dir.path(), &test_files));
}

#[test]
fn test_install_with_custom_flow_definition() {
    let input_dir = tempdir().unwrap();
    let output_dir = tempdir().unwrap();
    let install_dir = tempdir().unwrap();

    let test_files: Vec<(&str, &[u8])> = vec![
        ("app.exe", b"exe content"),
        ("data/config.json", br#"{"ok":true}"#),
    ];
    create_test_files(input_dir.path(), &test_files);

    let output_path = output_dir.path().join("custom-flow.pkg");
    let packager = Packager::new(PackagerConfig::default()).unwrap();
    packager
        .build_package(input_dir.path(), &output_path, None, |_| {})
        .unwrap();

    let custom_flow = FlowDefinition {
        version: 1,
        vars: std::collections::HashMap::new(),
        ui_flow: None,
        install_flow: InstallFlow {
            steps: vec![
                FlowStep {
                    id: "check_disk".to_string(),
                    step_type: "check_disk".to_string(),
                    params: json!({}),
                    when: None,
                    on_fail: Some(OnFailPolicy::Abort),
                    engine: None,
                },
                FlowStep {
                    id: "extract".to_string(),
                    step_type: "extract_package".to_string(),
                    params: json!({}),
                    when: None,
                    on_fail: Some(OnFailPolicy::Rollback),
                    engine: None,
                },
            ],
            rollback: vec![FlowStep {
                id: "rollback_files".to_string(),
                step_type: "rollback_files".to_string(),
                params: json!({}),
                when: None,
                on_fail: Some(OnFailPolicy::Continue),
                engine: None,
            }],
        },
    };

    let installer = Installer::new(output_path).unwrap();
    let options = InstallOptions {
        install_dir: install_dir.path().to_path_buf(),
        create_shortcuts: false,
        configure_registry: false,
        auto_startup: false,
        components: std::collections::BTreeMap::new(),
        silent: true,
        thread_count: None,
    };
    let stats = installer
        .install_with_flow_definition(options, custom_flow, |_| {})
        .unwrap();

    assert_eq!(stats.installed_files, 2);
    assert!(verify_installed_files(install_dir.path(), &test_files));
}

#[test]
fn test_flow_script_step_requires_policy() {
    let input_dir = tempdir().unwrap();
    let output_dir = tempdir().unwrap();
    let install_dir = tempdir().unwrap();
    let script_dir = tempdir().unwrap();
    let script_path = script_dir.path().join("ok.js");
    fs::write(&script_path, "true;").unwrap();

    create_test_files(input_dir.path(), &[("app.exe", b"exe content")]);
    let output_path = output_dir.path().join("script-policy.pkg");
    let packager = Packager::new(PackagerConfig::default()).unwrap();
    packager
        .build_package(input_dir.path(), &output_path, None, |_| {})
        .unwrap();

    let flow = FlowDefinition {
        version: 1,
        vars: std::collections::HashMap::new(),
        ui_flow: None,
        install_flow: InstallFlow {
            steps: vec![
                FlowStep {
                    id: "pre_script".to_string(),
                    step_type: "script".to_string(),
                    params: json!({ "path": script_path.to_string_lossy().to_string() }),
                    when: None,
                    on_fail: Some(OnFailPolicy::Abort),
                    engine: Some(installer_shared::ScriptEngine::Js),
                },
                FlowStep {
                    id: "extract".to_string(),
                    step_type: "extract_package".to_string(),
                    params: json!({}),
                    when: None,
                    on_fail: Some(OnFailPolicy::Rollback),
                    engine: None,
                },
            ],
            rollback: vec![FlowStep {
                id: "rollback_files".to_string(),
                step_type: "rollback_files".to_string(),
                params: json!({}),
                when: None,
                on_fail: Some(OnFailPolicy::Continue),
                engine: None,
            }],
        },
    };

    let installer = Installer::new(output_path).unwrap();
    let options = InstallOptions {
        install_dir: install_dir.path().to_path_buf(),
        create_shortcuts: false,
        configure_registry: false,
        auto_startup: false,
        components: std::collections::BTreeMap::new(),
        silent: true,
        thread_count: None,
    };

    let err = installer
        .install_with_flow_definition(options, flow, |_| {})
        .expect_err("script should be blocked by default");
    assert!(matches!(
        err,
        installer_shared::InstallerError::PermissionDenied(_)
    ));
}

#[test]
fn test_flow_script_step_executes_with_allowlist_policy() {
    if !has_node_runtime() {
        return;
    }
    let input_dir = tempdir().unwrap();
    let output_dir = tempdir().unwrap();
    let install_dir = tempdir().unwrap();
    let script_dir = tempdir().unwrap();
    let script_path = script_dir.path().join("ok.js");
    fs::write(&script_path, "true;").unwrap();

    create_test_files(input_dir.path(), &[("app.exe", b"exe content")]);
    let output_path = output_dir.path().join("script-enabled.pkg");
    let packager = Packager::new(PackagerConfig::default()).unwrap();
    packager
        .build_package(input_dir.path(), &output_path, None, |_| {})
        .unwrap();

    let flow = FlowDefinition {
        version: 1,
        vars: std::collections::HashMap::new(),
        ui_flow: None,
        install_flow: InstallFlow {
            steps: vec![
                FlowStep {
                    id: "pre_script".to_string(),
                    step_type: "script".to_string(),
                    params: json!({ "path": script_path.to_string_lossy().to_string() }),
                    when: None,
                    on_fail: Some(OnFailPolicy::Abort),
                    engine: Some(installer_shared::ScriptEngine::Js),
                },
                FlowStep {
                    id: "extract".to_string(),
                    step_type: "extract_package".to_string(),
                    params: json!({}),
                    when: None,
                    on_fail: Some(OnFailPolicy::Rollback),
                    engine: None,
                },
            ],
            rollback: vec![FlowStep {
                id: "rollback_files".to_string(),
                step_type: "rollback_files".to_string(),
                params: json!({}),
                when: None,
                on_fail: Some(OnFailPolicy::Continue),
                engine: None,
            }],
        },
    };

    let installer =
        Installer::new(output_path)
            .unwrap()
            .with_script_policy(ScriptPolicy::enabled_with_roots(vec![script_dir
                .path()
                .to_path_buf()]));
    let options = InstallOptions {
        install_dir: install_dir.path().to_path_buf(),
        create_shortcuts: false,
        configure_registry: false,
        auto_startup: false,
        components: std::collections::BTreeMap::new(),
        silent: true,
        thread_count: None,
    };

    let stats = installer
        .install_with_flow_definition(options, flow, |_| {})
        .expect("script step should pass");
    assert_eq!(stats.installed_files, 1);
    assert!(install_dir.path().join("app.exe").exists());
}

#[test]
fn test_install_flow_failure_triggers_rollback_cleanup() {
    let input_dir = tempdir().unwrap();
    let output_dir = tempdir().unwrap();
    let install_dir = tempdir().unwrap();

    let test_files: Vec<(&str, &[u8])> = vec![
        ("app.exe", b"exe content"),
        ("data/config.json", br#"{"ok":true}"#),
    ];
    create_test_files(input_dir.path(), &test_files);

    let output_path = output_dir.path().join("rollback-failure.pkg");
    let packager = Packager::new(PackagerConfig::default()).unwrap();
    packager
        .build_package(input_dir.path(), &output_path, None, |_| {})
        .unwrap();

    // 故障注入：extract 成功后执行一个未知 step，触发 on_fail=rollback。
    let flow = FlowDefinition {
        version: 1,
        vars: std::collections::HashMap::new(),
        ui_flow: None,
        install_flow: InstallFlow {
            steps: vec![
                FlowStep {
                    id: "extract".to_string(),
                    step_type: "extract_package".to_string(),
                    params: json!({}),
                    when: None,
                    on_fail: Some(OnFailPolicy::Rollback),
                    engine: None,
                },
                FlowStep {
                    id: "force_fail".to_string(),
                    step_type: "unknown_step_type".to_string(),
                    params: json!({}),
                    when: None,
                    on_fail: Some(OnFailPolicy::Rollback),
                    engine: None,
                },
            ],
            rollback: vec![FlowStep {
                id: "rollback_files".to_string(),
                step_type: "rollback_files".to_string(),
                params: json!({}),
                when: None,
                on_fail: Some(OnFailPolicy::Continue),
                engine: None,
            }],
        },
    };

    let installer = Installer::new(output_path).unwrap();
    let options = InstallOptions {
        install_dir: install_dir.path().to_path_buf(),
        create_shortcuts: false,
        configure_registry: false,
        auto_startup: false,
        components: std::collections::BTreeMap::new(),
        silent: true,
        thread_count: None,
    };

    let result = installer.install_with_flow_definition(options, flow, |_| {});
    assert!(result.is_err(), "flow should fail after fault injection");

    // 验证已安装文件被回滚清理。
    assert!(
        !install_dir.path().join("app.exe").exists(),
        "app.exe should be removed by rollback"
    );
    assert!(
        !install_dir.path().join("data").join("config.json").exists(),
        "config.json should be removed by rollback"
    );
}

#[test]
fn test_install_flow_returns_rollback_error_when_rollback_step_fails() {
    let input_dir = tempdir().unwrap();
    let output_dir = tempdir().unwrap();
    let install_dir = tempdir().unwrap();

    create_test_files(input_dir.path(), &[("app.exe", b"exe content")]);

    let output_path = output_dir.path().join("rollback-error.pkg");
    let packager = Packager::new(PackagerConfig::default()).unwrap();
    packager
        .build_package(input_dir.path(), &output_path, None, |_| {})
        .unwrap();

    // 主流程中故障注入失败，并触发 rollback；
    // rollback 本身使用未知步骤类型，期望返回 InstallerError::Rollback。
    let flow = FlowDefinition {
        version: 1,
        vars: std::collections::HashMap::new(),
        ui_flow: None,
        install_flow: InstallFlow {
            steps: vec![FlowStep {
                id: "force_fail".to_string(),
                step_type: "unknown_step_type".to_string(),
                params: json!({}),
                when: None,
                on_fail: Some(OnFailPolicy::Rollback),
                engine: None,
            }],
            rollback: vec![FlowStep {
                id: "bad_rollback".to_string(),
                step_type: "unknown_rollback_type".to_string(),
                params: json!({}),
                when: None,
                on_fail: Some(OnFailPolicy::Continue),
                engine: None,
            }],
        },
    };

    let installer = Installer::new(output_path).unwrap();
    let options = InstallOptions {
        install_dir: install_dir.path().to_path_buf(),
        create_shortcuts: false,
        configure_registry: false,
        auto_startup: false,
        components: std::collections::BTreeMap::new(),
        silent: true,
        thread_count: None,
    };

    let err = installer
        .install_with_flow_definition(options, flow, |_| {})
        .expect_err("rollback should fail");

    assert!(
        matches!(err, installer_shared::InstallerError::Rollback(_)),
        "expected Rollback error, got: {err:?}"
    );
}

#[test]
fn test_install_uses_embedded_flow_from_package_metadata() {
    let input_dir = tempdir().unwrap();
    let output_dir = tempdir().unwrap();
    let flow_dir = tempdir().unwrap();
    let install_dir = tempdir().unwrap();

    create_test_files(input_dir.path(), &[("app.exe", b"exe content")]);

    let flow_path = flow_dir.path().join("embedded-flow.yaml");
    fs::write(
        &flow_path,
        r#"
version: 1
install_flow:
  steps:
    - id: force_fail
      type: unknown_step_type
      on_fail: abort
  rollback: []
"#,
    )
    .unwrap();

    let output_path = output_dir.path().join("embedded-flow.pkg");
    let config = PackagerConfig {
        flow_file: Some(flow_path.clone()),
        ..Default::default()
    };
    let packager = Packager::new(config).unwrap();
    packager
        .build_package(input_dir.path(), &output_path, None, |_| {})
        .unwrap();

    let installer = Installer::new(output_path).unwrap();
    let options = InstallOptions {
        install_dir: install_dir.path().to_path_buf(),
        create_shortcuts: false,
        configure_registry: false,
        auto_startup: false,
        components: std::collections::BTreeMap::new(),
        silent: true,
        thread_count: None,
    };

    let err = installer
        .install(options, |_| {})
        .expect_err("embedded flow should override default and fail");
    assert!(matches!(err, installer_shared::InstallerError::Config(_)));
    assert!(
        !install_dir.path().join("app.exe").exists(),
        "no files should be extracted when embedded flow fails first"
    );
}

#[test]
fn test_install_embedded_script_runs_without_explicit_script_policy() {
    if !has_node_runtime() {
        return;
    }

    let input_dir = tempdir().unwrap();
    let output_dir = tempdir().unwrap();
    let install_dir = tempdir().unwrap();

    create_test_files(input_dir.path(), &[("app.exe", b"exe content")]);

    let scripts_dir = input_dir.path().join("scripts");
    fs::create_dir_all(&scripts_dir).unwrap();
    let script_path = scripts_dir.join("embedded_ok.js");
    fs::write(&script_path, "process.exit(0);\n").unwrap();

    let flow_path = input_dir.path().join("embedded-flow-script.yaml");
    fs::write(
        &flow_path,
        r#"
version: 1
install_flow:
  steps:
    - id: precheck
      type: script
      engine: js
      params:
        path: "scripts/embedded_ok.js"
      on_fail: abort
    - id: extract
      type: extract_package
      on_fail: rollback
  rollback:
    - id: rollback_files
      type: rollback_files
      on_fail: continue
"#,
    )
    .unwrap();

    let output_path = output_dir.path().join("embedded-script.pkg");
    let config = PackagerConfig {
        flow_file: Some(flow_path.clone()),
        script_files: vec![PathBuf::from("scripts/embedded_ok.js")],
        ..Default::default()
    };
    let packager = Packager::new(config).unwrap();
    packager
        .build_package(input_dir.path(), &output_path, None, |_| {})
        .unwrap();

    let installer = Installer::new(output_path).unwrap();
    let options = InstallOptions {
        install_dir: install_dir.path().to_path_buf(),
        create_shortcuts: false,
        configure_registry: false,
        auto_startup: false,
        components: std::collections::BTreeMap::new(),
        silent: true,
        thread_count: None,
    };
    let stats = installer
        .install(options, |_| {})
        .expect("embedded script should run without explicit script policy flags");

    assert_eq!(stats.installed_files, 1);
    assert!(install_dir.path().join("app.exe").exists());
}

#[test]
fn test_component_nodes_skeleton_load_download_verify() {
    let input_dir = tempdir().unwrap();
    let output_dir = tempdir().unwrap();
    let install_dir = tempdir().unwrap();
    let source_dir = tempdir().unwrap();

    create_test_files(input_dir.path(), &[("app.exe", b"exe content")]);

    let component_bin = source_dir.path().join("extra-tools.zip");
    fs::write(&component_bin, b"component-binary-content").unwrap();
    let component_sha = installer_core::sha256_file_hex(&component_bin).unwrap();

    let manifest_path = input_dir.path().join("component_manifest.yaml");
    let manifest = format!(
        r#"
version: 1
components:
  - id: "extra-tools"
    display_name: "Extra Tools"
    version: "1.0.0"
    package:
      url: 'file://{url}'
      size: 24
      sha256: '{sha}'
    install:
      kind: "archive"
      target_subdir: "components/extra-tools"
"#,
        url = component_bin.to_string_lossy(),
        sha = component_sha
    );
    fs::write(&manifest_path, manifest).unwrap();

    let output_path = output_dir.path().join("component-skeleton.pkg");
    let packager = Packager::new(PackagerConfig::default()).unwrap();
    packager
        .build_package(input_dir.path(), &output_path, None, |_| {})
        .unwrap();

    let flow = FlowDefinition {
        version: 1,
        vars: std::collections::HashMap::new(),
        ui_flow: None,
        install_flow: InstallFlow {
            steps: vec![
                FlowStep {
                    id: "extract".to_string(),
                    step_type: "extract_package".to_string(),
                    params: json!({}),
                    when: None,
                    on_fail: Some(OnFailPolicy::Rollback),
                    engine: None,
                },
                FlowStep {
                    id: "load_manifest".to_string(),
                    step_type: "load_component_manifest".to_string(),
                    params: json!({ "path": "${InstallDir}/component_manifest.yaml" }),
                    when: None,
                    on_fail: Some(OnFailPolicy::Abort),
                    engine: None,
                },
                FlowStep {
                    id: "download".to_string(),
                    step_type: "download_component".to_string(),
                    params: json!({ "component_id": "extra-tools" }),
                    when: None,
                    on_fail: Some(OnFailPolicy::Abort),
                    engine: None,
                },
                FlowStep {
                    id: "verify".to_string(),
                    step_type: "verify_component".to_string(),
                    params: json!({ "component_id": "extra-tools" }),
                    when: None,
                    on_fail: Some(OnFailPolicy::Abort),
                    engine: None,
                },
                FlowStep {
                    id: "install_component".to_string(),
                    step_type: "install_component".to_string(),
                    params: json!({ "component_id": "extra-tools" }),
                    when: None,
                    on_fail: Some(OnFailPolicy::Rollback),
                    engine: None,
                },
            ],
            rollback: vec![FlowStep {
                id: "rollback_files".to_string(),
                step_type: "rollback_files".to_string(),
                params: json!({}),
                when: None,
                on_fail: Some(OnFailPolicy::Continue),
                engine: None,
            }],
        },
    };

    let installer = Installer::new(output_path).unwrap();
    let options = InstallOptions {
        install_dir: install_dir.path().to_path_buf(),
        create_shortcuts: false,
        configure_registry: false,
        auto_startup: false,
        components: std::collections::BTreeMap::new(),
        silent: true,
        thread_count: None,
    };
    let stats = installer
        .install_with_flow_definition(options, flow, |_| {})
        .expect("component skeleton nodes should pass");

    assert_eq!(stats.installed_files, 2);
    assert!(install_dir.path().join("app.exe").exists());
    assert!(install_dir.path().join("component_manifest.yaml").exists());
    assert!(install_dir
        .path()
        .join("components")
        .join("extra-tools")
        .exists());
}

#[test]
fn test_component_install_rollback_cleans_component_files() {
    let input_dir = tempdir().unwrap();
    let output_dir = tempdir().unwrap();
    let install_dir = tempdir().unwrap();
    let source_dir = tempdir().unwrap();

    create_test_files(input_dir.path(), &[("app.exe", b"exe content")]);

    let component_bin = source_dir.path().join("extra-tools.zip");
    fs::write(&component_bin, b"component-binary-content").unwrap();
    let component_sha = installer_core::sha256_file_hex(&component_bin).unwrap();

    let manifest_path = input_dir.path().join("component_manifest.yaml");
    let manifest = format!(
        r#"
version: 1
components:
  - id: "extra-tools"
    display_name: "Extra Tools"
    version: "1.0.0"
    package:
      url: 'file://{url}'
      size: 24
      sha256: '{sha}'
    install:
      kind: "archive"
      target_subdir: "components/extra-tools"
"#,
        url = component_bin.to_string_lossy(),
        sha = component_sha
    );
    fs::write(&manifest_path, manifest).unwrap();

    let output_path = output_dir.path().join("component-rollback.pkg");
    let packager = Packager::new(PackagerConfig::default()).unwrap();
    packager
        .build_package(input_dir.path(), &output_path, None, |_| {})
        .unwrap();

    let flow = FlowDefinition {
        version: 1,
        vars: std::collections::HashMap::new(),
        ui_flow: None,
        install_flow: InstallFlow {
            steps: vec![
                FlowStep {
                    id: "extract".to_string(),
                    step_type: "extract_package".to_string(),
                    params: json!({}),
                    when: None,
                    on_fail: Some(OnFailPolicy::Rollback),
                    engine: None,
                },
                FlowStep {
                    id: "load_manifest".to_string(),
                    step_type: "load_component_manifest".to_string(),
                    params: json!({ "path": "${InstallDir}/component_manifest.yaml" }),
                    when: None,
                    on_fail: Some(OnFailPolicy::Abort),
                    engine: None,
                },
                FlowStep {
                    id: "download".to_string(),
                    step_type: "download_component".to_string(),
                    params: json!({ "component_id": "extra-tools" }),
                    when: None,
                    on_fail: Some(OnFailPolicy::Abort),
                    engine: None,
                },
                FlowStep {
                    id: "verify".to_string(),
                    step_type: "verify_component".to_string(),
                    params: json!({ "component_id": "extra-tools" }),
                    when: None,
                    on_fail: Some(OnFailPolicy::Abort),
                    engine: None,
                },
                FlowStep {
                    id: "install_component".to_string(),
                    step_type: "install_component".to_string(),
                    params: json!({ "component_id": "extra-tools" }),
                    when: None,
                    on_fail: Some(OnFailPolicy::Rollback),
                    engine: None,
                },
                FlowStep {
                    id: "force_fail".to_string(),
                    step_type: "unknown_step_type".to_string(),
                    params: json!({}),
                    when: None,
                    on_fail: Some(OnFailPolicy::Rollback),
                    engine: None,
                },
            ],
            rollback: vec![
                FlowStep {
                    id: "rollback_components".to_string(),
                    step_type: "rollback_component".to_string(),
                    params: json!({}),
                    when: None,
                    on_fail: Some(OnFailPolicy::Continue),
                    engine: None,
                },
                FlowStep {
                    id: "rollback_files".to_string(),
                    step_type: "rollback_files".to_string(),
                    params: json!({}),
                    when: None,
                    on_fail: Some(OnFailPolicy::Continue),
                    engine: None,
                },
            ],
        },
    };

    let installer = Installer::new(output_path).unwrap();
    let options = InstallOptions {
        install_dir: install_dir.path().to_path_buf(),
        create_shortcuts: false,
        configure_registry: false,
        auto_startup: false,
        components: std::collections::BTreeMap::new(),
        silent: true,
        thread_count: None,
    };

    let result = installer.install_with_flow_definition(options, flow, |_| {});
    assert!(result.is_err());
    assert!(!install_dir
        .path()
        .join("components")
        .join("extra-tools")
        .exists());
}

#[test]
fn test_resolve_selected_components_supports_required_component() {
    let input_dir = tempdir().unwrap();
    let output_dir = tempdir().unwrap();
    let install_dir = tempdir().unwrap();
    let source_dir = tempdir().unwrap();

    create_test_files(input_dir.path(), &[("app.exe", b"exe content")]);

    let component_bin = source_dir.path().join("required-tools.zip");
    fs::write(&component_bin, b"component-required-content").unwrap();
    let component_sha = installer_core::sha256_file_hex(&component_bin).unwrap();

    let manifest_path = input_dir.path().join("component_manifest.yaml");
    let manifest = format!(
        r#"
version: 1
components:
  - id: "required-tools"
    display_name: "Required Tools"
    version: "1.0.0"
    required: true
    package:
      url: 'file://{url}'
      size: 26
      sha256: '{sha}'
    install:
      kind: "archive"
      target_subdir: "components/required-tools"
"#,
        url = component_bin.to_string_lossy(),
        sha = component_sha
    );
    fs::write(&manifest_path, manifest).unwrap();

    let output_path = output_dir.path().join("component-required.pkg");
    let packager = Packager::new(PackagerConfig::default()).unwrap();
    packager
        .build_package(input_dir.path(), &output_path, None, |_| {})
        .unwrap();

    let flow = FlowDefinition {
        version: 1,
        vars: std::collections::HashMap::new(),
        ui_flow: None,
        install_flow: InstallFlow {
            steps: vec![
                FlowStep {
                    id: "extract".to_string(),
                    step_type: "extract_package".to_string(),
                    params: json!({}),
                    when: None,
                    on_fail: Some(OnFailPolicy::Rollback),
                    engine: None,
                },
                FlowStep {
                    id: "load_manifest".to_string(),
                    step_type: "load_component_manifest".to_string(),
                    params: json!({ "path": "${InstallDir}/component_manifest.yaml" }),
                    when: None,
                    on_fail: Some(OnFailPolicy::Abort),
                    engine: None,
                },
                FlowStep {
                    id: "resolve".to_string(),
                    step_type: "resolve_selected_components".to_string(),
                    params: json!({ "include_required": true }),
                    when: None,
                    on_fail: Some(OnFailPolicy::Abort),
                    engine: None,
                },
                FlowStep {
                    id: "download".to_string(),
                    step_type: "download_component".to_string(),
                    params: json!({}),
                    when: None,
                    on_fail: Some(OnFailPolicy::Abort),
                    engine: None,
                },
                FlowStep {
                    id: "verify".to_string(),
                    step_type: "verify_component".to_string(),
                    params: json!({}),
                    when: None,
                    on_fail: Some(OnFailPolicy::Abort),
                    engine: None,
                },
                FlowStep {
                    id: "install_component".to_string(),
                    step_type: "install_component".to_string(),
                    params: json!({}),
                    when: None,
                    on_fail: Some(OnFailPolicy::Rollback),
                    engine: None,
                },
            ],
            rollback: vec![FlowStep {
                id: "rollback_files".to_string(),
                step_type: "rollback_files".to_string(),
                params: json!({}),
                when: None,
                on_fail: Some(OnFailPolicy::Continue),
                engine: None,
            }],
        },
    };

    let installer = Installer::new(output_path).unwrap();
    let options = InstallOptions {
        install_dir: install_dir.path().to_path_buf(),
        create_shortcuts: false,
        configure_registry: false,
        auto_startup: false,
        components: std::collections::BTreeMap::new(),
        silent: true,
        thread_count: None,
    };
    let stats = installer
        .install_with_flow_definition(options, flow, |_| {})
        .expect("required component should be resolved and installed");

    assert_eq!(stats.installed_files, 2);
    assert!(install_dir.path().join("app.exe").exists());
    assert!(install_dir
        .path()
        .join("components")
        .join("required-tools")
        .join("required-tools-1.0.0.zip")
        .exists());
}

#[test]
fn test_resolve_selected_components_supports_ui_selected_component() {
    let input_dir = tempdir().unwrap();
    let output_dir = tempdir().unwrap();
    let install_dir = tempdir().unwrap();
    let source_dir = tempdir().unwrap();

    create_test_files(input_dir.path(), &[("app.exe", b"exe content")]);

    let component_bin = source_dir.path().join("extra-tools.zip");
    fs::write(&component_bin, b"component-selected-content").unwrap();
    let component_sha = installer_core::sha256_file_hex(&component_bin).unwrap();

    let manifest_path = input_dir.path().join("component_manifest.yaml");
    let manifest = format!(
        r#"
version: 1
components:
  - id: "extra-tools"
    display_name: "Extra Tools"
    version: "1.0.0"
    required: false
    package:
      url: 'file://{url}'
      size: 26
      sha256: '{sha}'
    install:
      kind: "archive"
      target_subdir: "components/extra-tools"
"#,
        url = component_bin.to_string_lossy(),
        sha = component_sha
    );
    fs::write(&manifest_path, manifest).unwrap();

    let output_path = output_dir.path().join("component-selected.pkg");
    let packager = Packager::new(PackagerConfig::default()).unwrap();
    packager
        .build_package(input_dir.path(), &output_path, None, |_| {})
        .unwrap();

    let flow = FlowDefinition {
        version: 1,
        vars: std::collections::HashMap::new(),
        ui_flow: None,
        install_flow: InstallFlow {
            steps: vec![
                FlowStep {
                    id: "extract".to_string(),
                    step_type: "extract_package".to_string(),
                    params: json!({}),
                    when: None,
                    on_fail: Some(OnFailPolicy::Rollback),
                    engine: None,
                },
                FlowStep {
                    id: "load_manifest".to_string(),
                    step_type: "load_component_manifest".to_string(),
                    params: json!({ "path": "${InstallDir}/component_manifest.yaml" }),
                    when: None,
                    on_fail: Some(OnFailPolicy::Abort),
                    engine: None,
                },
                FlowStep {
                    id: "resolve".to_string(),
                    step_type: "resolve_selected_components".to_string(),
                    params: json!({ "include_required": true }),
                    when: None,
                    on_fail: Some(OnFailPolicy::Abort),
                    engine: None,
                },
                FlowStep {
                    id: "download".to_string(),
                    step_type: "download_component".to_string(),
                    params: json!({}),
                    when: None,
                    on_fail: Some(OnFailPolicy::Abort),
                    engine: None,
                },
                FlowStep {
                    id: "verify".to_string(),
                    step_type: "verify_component".to_string(),
                    params: json!({}),
                    when: None,
                    on_fail: Some(OnFailPolicy::Abort),
                    engine: None,
                },
                FlowStep {
                    id: "install_component".to_string(),
                    step_type: "install_component".to_string(),
                    params: json!({}),
                    when: None,
                    on_fail: Some(OnFailPolicy::Rollback),
                    engine: None,
                },
            ],
            rollback: vec![FlowStep {
                id: "rollback_files".to_string(),
                step_type: "rollback_files".to_string(),
                params: json!({}),
                when: None,
                on_fail: Some(OnFailPolicy::Continue),
                engine: None,
            }],
        },
    };

    let installer = Installer::new(output_path).unwrap();
    let mut components = std::collections::BTreeMap::new();
    components.insert("extra-tools".to_string(), true);
    let options = InstallOptions {
        install_dir: install_dir.path().to_path_buf(),
        create_shortcuts: false,
        configure_registry: false,
        auto_startup: false,
        components,
        silent: true,
        thread_count: None,
    };
    let stats = installer
        .install_with_flow_definition(options, flow, |_| {})
        .expect("selected component should be resolved and installed");

    assert_eq!(stats.installed_files, 2);
    assert!(install_dir.path().join("app.exe").exists());
    assert!(install_dir
        .path()
        .join("components")
        .join("extra-tools")
        .join("extra-tools-1.0.0.zip")
        .exists());
}

#[test]
fn test_component_nodes_batch_selected_components() {
    let input_dir = tempdir().unwrap();
    let output_dir = tempdir().unwrap();
    let install_dir = tempdir().unwrap();
    let source_dir = tempdir().unwrap();

    create_test_files(input_dir.path(), &[("app.exe", b"exe content")]);

    let c1 = source_dir.path().join("extra-tools.zip");
    let c2 = source_dir.path().join("lang-pack.zip");
    fs::write(&c1, b"component-extra-content").unwrap();
    fs::write(&c2, b"component-lang-content").unwrap();
    let c1_sha = installer_core::sha256_file_hex(&c1).unwrap();
    let c2_sha = installer_core::sha256_file_hex(&c2).unwrap();

    let manifest_path = input_dir.path().join("component_manifest.yaml");
    let manifest = format!(
        r#"
version: 1
components:
  - id: "extra-tools"
    display_name: "Extra Tools"
    version: "1.0.0"
    required: false
    package:
      url: 'file://{c1_url}'
      size: 23
      sha256: '{c1_sha}'
    install:
      kind: "archive"
      target_subdir: "components/extra-tools"
  - id: "lang-pack"
    display_name: "Language Pack"
    version: "1.0.0"
    required: false
    package:
      url: 'file://{c2_url}'
      size: 22
      sha256: '{c2_sha}'
    install:
      kind: "archive"
      target_subdir: "components/lang-pack"
"#,
        c1_url = c1.to_string_lossy(),
        c1_sha = c1_sha,
        c2_url = c2.to_string_lossy(),
        c2_sha = c2_sha
    );
    fs::write(&manifest_path, manifest).unwrap();

    let output_path = output_dir.path().join("component-batch.pkg");
    let packager = Packager::new(PackagerConfig::default()).unwrap();
    packager
        .build_package(input_dir.path(), &output_path, None, |_| {})
        .unwrap();

    let flow = FlowDefinition {
        version: 1,
        vars: std::collections::HashMap::new(),
        ui_flow: None,
        install_flow: InstallFlow {
            steps: vec![
                FlowStep {
                    id: "extract".to_string(),
                    step_type: "extract_package".to_string(),
                    params: json!({}),
                    when: None,
                    on_fail: Some(OnFailPolicy::Rollback),
                    engine: None,
                },
                FlowStep {
                    id: "load_manifest".to_string(),
                    step_type: "load_component_manifest".to_string(),
                    params: json!({ "path": "${InstallDir}/component_manifest.yaml" }),
                    when: None,
                    on_fail: Some(OnFailPolicy::Abort),
                    engine: None,
                },
                FlowStep {
                    id: "resolve".to_string(),
                    step_type: "resolve_selected_components".to_string(),
                    params: json!({ "include_required": true }),
                    when: None,
                    on_fail: Some(OnFailPolicy::Abort),
                    engine: None,
                },
                FlowStep {
                    id: "download_all".to_string(),
                    step_type: "download_component".to_string(),
                    params: json!({}),
                    when: None,
                    on_fail: Some(OnFailPolicy::Abort),
                    engine: None,
                },
                FlowStep {
                    id: "verify_all".to_string(),
                    step_type: "verify_component".to_string(),
                    params: json!({}),
                    when: None,
                    on_fail: Some(OnFailPolicy::Abort),
                    engine: None,
                },
                FlowStep {
                    id: "install_all".to_string(),
                    step_type: "install_component".to_string(),
                    params: json!({}),
                    when: None,
                    on_fail: Some(OnFailPolicy::Rollback),
                    engine: None,
                },
            ],
            rollback: vec![FlowStep {
                id: "rollback_files".to_string(),
                step_type: "rollback_files".to_string(),
                params: json!({}),
                when: None,
                on_fail: Some(OnFailPolicy::Continue),
                engine: None,
            }],
        },
    };

    let installer = Installer::new(output_path).unwrap();
    let mut components = std::collections::BTreeMap::new();
    components.insert("extra-tools".to_string(), true);
    components.insert("lang-pack".to_string(), true);
    let options = InstallOptions {
        install_dir: install_dir.path().to_path_buf(),
        create_shortcuts: false,
        configure_registry: false,
        auto_startup: false,
        components,
        silent: true,
        thread_count: None,
    };
    let stats = installer
        .install_with_flow_definition(options, flow, |_| {})
        .expect("batch selected components should install");

    assert_eq!(stats.installed_files, 2);
    assert!(install_dir.path().join("app.exe").exists());
    assert!(install_dir
        .path()
        .join("components")
        .join("extra-tools")
        .join("extra-tools-1.0.0.zip")
        .exists());
    assert!(install_dir
        .path()
        .join("components")
        .join("lang-pack")
        .join("lang-pack-1.0.0.zip")
        .exists());
}

#[test]
fn test_process_selected_components_node() {
    let input_dir = tempdir().unwrap();
    let output_dir = tempdir().unwrap();
    let install_dir = tempdir().unwrap();
    let source_dir = tempdir().unwrap();

    create_test_files(input_dir.path(), &[("app.exe", b"exe content")]);

    let c1 = source_dir.path().join("extra-tools.zip");
    let c2 = source_dir.path().join("lang-pack.zip");
    fs::write(&c1, b"component-extra-content").unwrap();
    fs::write(&c2, b"component-lang-content").unwrap();
    let c1_sha = installer_core::sha256_file_hex(&c1).unwrap();
    let c2_sha = installer_core::sha256_file_hex(&c2).unwrap();

    let manifest_path = input_dir.path().join("component_manifest.yaml");
    let manifest = format!(
        r#"
version: 1
components:
  - id: "extra-tools"
    display_name: "Extra Tools"
    version: "1.0.0"
    package:
      url: 'file://{c1_url}'
      size: 23
      sha256: '{c1_sha}'
    install:
      kind: "archive"
      target_subdir: "components/extra-tools"
  - id: "lang-pack"
    display_name: "Language Pack"
    version: "1.0.0"
    package:
      url: 'file://{c2_url}'
      size: 22
      sha256: '{c2_sha}'
    install:
      kind: "archive"
      target_subdir: "components/lang-pack"
"#,
        c1_url = c1.to_string_lossy(),
        c1_sha = c1_sha,
        c2_url = c2.to_string_lossy(),
        c2_sha = c2_sha
    );
    fs::write(&manifest_path, manifest).unwrap();

    let output_path = output_dir.path().join("component-process-selected.pkg");
    let packager = Packager::new(PackagerConfig::default()).unwrap();
    packager
        .build_package(input_dir.path(), &output_path, None, |_| {})
        .unwrap();

    let flow = FlowDefinition {
        version: 1,
        vars: std::collections::HashMap::new(),
        ui_flow: None,
        install_flow: InstallFlow {
            steps: vec![
                FlowStep {
                    id: "extract".to_string(),
                    step_type: "extract_package".to_string(),
                    params: json!({}),
                    when: None,
                    on_fail: Some(OnFailPolicy::Rollback),
                    engine: None,
                },
                FlowStep {
                    id: "load_manifest".to_string(),
                    step_type: "load_component_manifest".to_string(),
                    params: json!({ "path": "${InstallDir}/component_manifest.yaml" }),
                    when: None,
                    on_fail: Some(OnFailPolicy::Abort),
                    engine: None,
                },
                FlowStep {
                    id: "resolve".to_string(),
                    step_type: "resolve_selected_components".to_string(),
                    params: json!({}),
                    when: None,
                    on_fail: Some(OnFailPolicy::Abort),
                    engine: None,
                },
                FlowStep {
                    id: "process_download".to_string(),
                    step_type: "process_selected_components".to_string(),
                    params: json!({ "action": "download" }),
                    when: None,
                    on_fail: Some(OnFailPolicy::Abort),
                    engine: None,
                },
                FlowStep {
                    id: "process_verify".to_string(),
                    step_type: "process_selected_components".to_string(),
                    params: json!({ "action": "verify" }),
                    when: None,
                    on_fail: Some(OnFailPolicy::Abort),
                    engine: None,
                },
                FlowStep {
                    id: "process_install".to_string(),
                    step_type: "process_selected_components".to_string(),
                    params: json!({ "action": "install" }),
                    when: None,
                    on_fail: Some(OnFailPolicy::Rollback),
                    engine: None,
                },
            ],
            rollback: vec![FlowStep {
                id: "rollback_files".to_string(),
                step_type: "rollback_files".to_string(),
                params: json!({}),
                when: None,
                on_fail: Some(OnFailPolicy::Continue),
                engine: None,
            }],
        },
    };

    let installer = Installer::new(output_path).unwrap();
    let mut components = std::collections::BTreeMap::new();
    components.insert("extra-tools".to_string(), true);
    components.insert("lang-pack".to_string(), true);
    let options = InstallOptions {
        install_dir: install_dir.path().to_path_buf(),
        create_shortcuts: false,
        configure_registry: false,
        auto_startup: false,
        components,
        silent: true,
        thread_count: None,
    };
    let stats = installer
        .install_with_flow_definition(options, flow, |_| {})
        .expect("process_selected_components should handle selected components");

    assert_eq!(stats.installed_files, 2);
    assert!(install_dir
        .path()
        .join("components")
        .join("extra-tools")
        .join("extra-tools-1.0.0.zip")
        .exists());
    assert!(install_dir
        .path()
        .join("components")
        .join("lang-pack")
        .join("lang-pack-1.0.0.zip")
        .exists());
}

#[test]
fn test_component_download_failure_triggers_rollback_cleanup() {
    let input_dir = tempdir().unwrap();
    let output_dir = tempdir().unwrap();
    let install_dir = tempdir().unwrap();

    create_test_files(input_dir.path(), &[("app.exe", b"exe content")]);

    let manifest_path = input_dir.path().join("component_manifest.yaml");
    let manifest = r#"
version: 1
components:
  - id: "missing-tools"
    display_name: "Missing Tools"
    version: "1.0.0"
    package:
      url: "file:///definitely/not/exist/missing-tools.zip"
      size: 1
      sha256: "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
    install:
      kind: "archive"
      target_subdir: "components/missing-tools"
"#;
    fs::write(&manifest_path, manifest).unwrap();

    let output_path = output_dir.path().join("component-download-fail.pkg");
    let packager = Packager::new(PackagerConfig::default()).unwrap();
    packager
        .build_package(input_dir.path(), &output_path, None, |_| {})
        .unwrap();

    let flow = FlowDefinition {
        version: 1,
        vars: std::collections::HashMap::new(),
        ui_flow: None,
        install_flow: InstallFlow {
            steps: vec![
                FlowStep {
                    id: "extract".to_string(),
                    step_type: "extract_package".to_string(),
                    params: json!({}),
                    when: None,
                    on_fail: Some(OnFailPolicy::Rollback),
                    engine: None,
                },
                FlowStep {
                    id: "load_manifest".to_string(),
                    step_type: "load_component_manifest".to_string(),
                    params: json!({ "path": "${InstallDir}/component_manifest.yaml" }),
                    when: None,
                    on_fail: Some(OnFailPolicy::Abort),
                    engine: None,
                },
                FlowStep {
                    id: "download".to_string(),
                    step_type: "download_component".to_string(),
                    params: json!({ "component_id": "missing-tools" }),
                    when: None,
                    on_fail: Some(OnFailPolicy::Rollback),
                    engine: None,
                },
            ],
            rollback: vec![FlowStep {
                id: "rollback_files".to_string(),
                step_type: "rollback_files".to_string(),
                params: json!({}),
                when: None,
                on_fail: Some(OnFailPolicy::Continue),
                engine: None,
            }],
        },
    };

    let installer = Installer::new(output_path).unwrap();
    let options = InstallOptions {
        install_dir: install_dir.path().to_path_buf(),
        create_shortcuts: false,
        configure_registry: false,
        auto_startup: false,
        components: std::collections::BTreeMap::new(),
        silent: true,
        thread_count: None,
    };
    let result = installer.install_with_flow_definition(options, flow, |_| {});
    assert!(result.is_err());
    assert!(!install_dir.path().join("app.exe").exists());
}

#[test]
fn test_component_verify_failure_triggers_rollback_cleanup() {
    let input_dir = tempdir().unwrap();
    let output_dir = tempdir().unwrap();
    let install_dir = tempdir().unwrap();
    let source_dir = tempdir().unwrap();

    create_test_files(input_dir.path(), &[("app.exe", b"exe content")]);

    let component_bin = source_dir.path().join("verify-fail.zip");
    fs::write(&component_bin, b"verify-fail-content").unwrap();

    let manifest_path = input_dir.path().join("component_manifest.yaml");
    let manifest = format!(
        r#"
version: 1
components:
  - id: "verify-fail"
    display_name: "Verify Fail"
    version: "1.0.0"
    package:
      url: 'file://{url}'
      size: 19
      sha256: "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
    install:
      kind: "archive"
      target_subdir: "components/verify-fail"
"#,
        url = component_bin.to_string_lossy()
    );
    fs::write(&manifest_path, manifest).unwrap();

    let output_path = output_dir.path().join("component-verify-fail.pkg");
    let packager = Packager::new(PackagerConfig::default()).unwrap();
    packager
        .build_package(input_dir.path(), &output_path, None, |_| {})
        .unwrap();

    let flow = FlowDefinition {
        version: 1,
        vars: std::collections::HashMap::new(),
        ui_flow: None,
        install_flow: InstallFlow {
            steps: vec![
                FlowStep {
                    id: "extract".to_string(),
                    step_type: "extract_package".to_string(),
                    params: json!({}),
                    when: None,
                    on_fail: Some(OnFailPolicy::Rollback),
                    engine: None,
                },
                FlowStep {
                    id: "load_manifest".to_string(),
                    step_type: "load_component_manifest".to_string(),
                    params: json!({ "path": "${InstallDir}/component_manifest.yaml" }),
                    when: None,
                    on_fail: Some(OnFailPolicy::Abort),
                    engine: None,
                },
                FlowStep {
                    id: "download".to_string(),
                    step_type: "download_component".to_string(),
                    params: json!({ "component_id": "verify-fail" }),
                    when: None,
                    on_fail: Some(OnFailPolicy::Abort),
                    engine: None,
                },
                FlowStep {
                    id: "verify".to_string(),
                    step_type: "verify_component".to_string(),
                    params: json!({ "component_id": "verify-fail" }),
                    when: None,
                    on_fail: Some(OnFailPolicy::Rollback),
                    engine: None,
                },
            ],
            rollback: vec![FlowStep {
                id: "rollback_files".to_string(),
                step_type: "rollback_files".to_string(),
                params: json!({}),
                when: None,
                on_fail: Some(OnFailPolicy::Continue),
                engine: None,
            }],
        },
    };

    let installer = Installer::new(output_path).unwrap();
    let options = InstallOptions {
        install_dir: install_dir.path().to_path_buf(),
        create_shortcuts: false,
        configure_registry: false,
        auto_startup: false,
        components: std::collections::BTreeMap::new(),
        silent: true,
        thread_count: None,
    };
    let result = installer.install_with_flow_definition(options, flow, |_| {});
    assert!(result.is_err());
    assert!(!install_dir.path().join("app.exe").exists());
}

#[test]
fn test_component_install_failure_triggers_rollback_cleanup() {
    let input_dir = tempdir().unwrap();
    let output_dir = tempdir().unwrap();
    let install_dir = tempdir().unwrap();
    let source_dir = tempdir().unwrap();

    create_test_files(input_dir.path(), &[("app.exe", b"exe content")]);

    let component_bin = source_dir.path().join("install-fail.zip");
    fs::write(&component_bin, b"install-fail-content").unwrap();
    let component_sha = installer_core::sha256_file_hex(&component_bin).unwrap();

    let manifest_path = input_dir.path().join("component_manifest.yaml");
    let manifest = format!(
        r#"
version: 1
components:
  - id: "install-fail"
    display_name: "Install Fail"
    version: "1.0.0"
    package:
      url: 'file://{url}'
      size: 20
      sha256: '{sha}'
    install:
      kind: "unsupported_kind"
"#,
        url = component_bin.to_string_lossy(),
        sha = component_sha
    );
    fs::write(&manifest_path, manifest).unwrap();

    let output_path = output_dir.path().join("component-install-fail.pkg");
    let packager = Packager::new(PackagerConfig::default()).unwrap();
    packager
        .build_package(input_dir.path(), &output_path, None, |_| {})
        .unwrap();

    let flow = FlowDefinition {
        version: 1,
        vars: std::collections::HashMap::new(),
        ui_flow: None,
        install_flow: InstallFlow {
            steps: vec![
                FlowStep {
                    id: "extract".to_string(),
                    step_type: "extract_package".to_string(),
                    params: json!({}),
                    when: None,
                    on_fail: Some(OnFailPolicy::Rollback),
                    engine: None,
                },
                FlowStep {
                    id: "load_manifest".to_string(),
                    step_type: "load_component_manifest".to_string(),
                    params: json!({ "path": "${InstallDir}/component_manifest.yaml" }),
                    when: None,
                    on_fail: Some(OnFailPolicy::Abort),
                    engine: None,
                },
                FlowStep {
                    id: "download".to_string(),
                    step_type: "download_component".to_string(),
                    params: json!({ "component_id": "install-fail" }),
                    when: None,
                    on_fail: Some(OnFailPolicy::Abort),
                    engine: None,
                },
                FlowStep {
                    id: "verify".to_string(),
                    step_type: "verify_component".to_string(),
                    params: json!({ "component_id": "install-fail" }),
                    when: None,
                    on_fail: Some(OnFailPolicy::Abort),
                    engine: None,
                },
                FlowStep {
                    id: "install".to_string(),
                    step_type: "install_component".to_string(),
                    params: json!({ "component_id": "install-fail" }),
                    when: None,
                    on_fail: Some(OnFailPolicy::Rollback),
                    engine: None,
                },
            ],
            rollback: vec![FlowStep {
                id: "rollback_files".to_string(),
                step_type: "rollback_files".to_string(),
                params: json!({}),
                when: None,
                on_fail: Some(OnFailPolicy::Continue),
                engine: None,
            }],
        },
    };

    let installer = Installer::new(output_path).unwrap();
    let options = InstallOptions {
        install_dir: install_dir.path().to_path_buf(),
        create_shortcuts: false,
        configure_registry: false,
        auto_startup: false,
        components: std::collections::BTreeMap::new(),
        silent: true,
        thread_count: None,
    };
    let result = installer.install_with_flow_definition(options, flow, |_| {});
    assert!(result.is_err());
    assert!(!install_dir.path().join("app.exe").exists());
}

#[test]
fn test_packager_output_is_deterministic_for_same_input() {
    let input_dir = tempdir().unwrap();
    let output_dir = tempdir().unwrap();

    create_test_files(
        input_dir.path(),
        &[
            ("app.exe", b"exe content"),
            ("config/settings.json", br#"{"name":"demo"}"#),
            ("assets/readme.txt", b"hello"),
        ],
    );

    let config = PackagerConfig {
        application_name: "DeterministicDemo".to_string(),
        version: "1.0.0".to_string(),
        ..Default::default()
    };
    let packager = Packager::new(config).unwrap();

    let p1 = output_dir.path().join("a.pkg");
    let p2 = output_dir.path().join("b.pkg");
    packager
        .build_package(input_dir.path(), &p1, None, |_| {})
        .unwrap();
    packager
        .build_package(input_dir.path(), &p2, None, |_| {})
        .unwrap();

    let h1 = installer_core::sha256_file_hex(&p1).unwrap();
    let h2 = installer_core::sha256_file_hex(&p2).unwrap();
    assert_eq!(
        h1, h2,
        "same input should produce deterministic package bytes"
    );
}
#[test]
fn test_complete_install_flow_with_ui_resources() {
    let input_dir = tempdir().unwrap();
    let output_dir = tempdir().unwrap();
    let ui_dir = tempdir().unwrap();
    let install_dir = tempdir().unwrap();

    let test_files: Vec<(&str, &[u8])> = vec![("app.exe", b"exe"), ("lib/helper.dll", b"dll")];
    create_test_files(input_dir.path(), &test_files);
    create_ui_resources(ui_dir.path());

    let output_path = output_dir.path().join("ui.pkg");
    let config = PackagerConfig::default();
    let packager = Packager::new(config).unwrap();
    packager
        .build_package(input_dir.path(), &output_path, Some(ui_dir.path()), |_| {})
        .unwrap();

    let installer = Installer::new(output_path.clone()).unwrap();
    assert!(installer.has_ui_resources().unwrap());

    let options = InstallOptions {
        install_dir: install_dir.path().to_path_buf(),
        create_shortcuts: false,
        configure_registry: false,
        auto_startup: false,
        components: std::collections::BTreeMap::new(),
        silent: true,
        thread_count: None,
    };
    installer.install(options, |_| {}).unwrap();
    assert!(verify_installed_files(install_dir.path(), &test_files));

    let ui_extract = tempdir().unwrap();
    let installer2 = Installer::new(output_path).unwrap();
    let ui = installer2.extract_ui_resources(ui_extract.path()).unwrap();
    assert!(ui.is_some());
    assert!(ui_extract.path().join("index.html").exists());
}

#[test]
fn test_complete_install_flow_large_files() {
    let input_dir = tempdir().unwrap();
    let output_dir = tempdir().unwrap();
    let install_dir = tempdir().unwrap();

    let large: Vec<u8> = (0..1024 * 1024).map(|i| (i % 256) as u8).collect();
    fs::write(input_dir.path().join("large.bin"), &large).unwrap();
    fs::write(input_dir.path().join("small.txt"), b"small").unwrap();

    let output_path = output_dir.path().join("large.pkg");
    let config = PackagerConfig {
        block_size: 256 * 1024,
        ..Default::default()
    };
    let packager = Packager::new(config).unwrap();
    let stats = packager
        .build_package(input_dir.path(), &output_path, None, |_| {})
        .unwrap();
    assert!(stats.total_size >= 1024 * 1024);

    let installer = Installer::new(output_path).unwrap();
    let options = InstallOptions {
        install_dir: install_dir.path().to_path_buf(),
        create_shortcuts: false,
        configure_registry: false,
        auto_startup: false,
        components: std::collections::BTreeMap::new(),
        silent: true,
        thread_count: Some(4),
    };
    installer.install(options, |_| {}).unwrap();

    let installed = fs::read(install_dir.path().join("large.bin")).unwrap();
    assert_eq!(installed, large);
}

#[test]
fn test_complete_install_flow_subdirectories() {
    let input_dir = tempdir().unwrap();
    let output_dir = tempdir().unwrap();
    let install_dir = tempdir().unwrap();

    let test_files: Vec<(&str, &[u8])> = vec![
        ("root.txt", b"root"),
        ("l1/f1.txt", b"l1"),
        ("l1/l2/f2.txt", b"l2"),
        ("l1/l2/l3/f3.txt", b"l3"),
    ];
    create_test_files(input_dir.path(), &test_files);

    let output_path = output_dir.path().join("nested.pkg");
    let packager = Packager::new(PackagerConfig::default()).unwrap();
    packager
        .build_package(input_dir.path(), &output_path, None, |_| {})
        .unwrap();

    let installer = Installer::new(output_path).unwrap();
    let options = InstallOptions {
        install_dir: install_dir.path().to_path_buf(),
        create_shortcuts: false,
        configure_registry: false,
        auto_startup: false,
        components: std::collections::BTreeMap::new(),
        silent: true,
        thread_count: None,
    };
    installer.install(options, |_| {}).unwrap();

    assert!(verify_installed_files(install_dir.path(), &test_files));
    assert!(install_dir.path().join("l1/l2/l3").is_dir());
}

#[test]
fn test_complete_install_flow_progress_events() {
    let input_dir = tempdir().unwrap();
    let output_dir = tempdir().unwrap();
    let install_dir = tempdir().unwrap();

    for i in 0..10 {
        fs::write(
            input_dir.path().join(format!("f{}.txt", i)),
            format!("c{}", i),
        )
        .unwrap();
    }

    let output_path = output_dir.path().join("progress.pkg");
    let packager = Packager::new(PackagerConfig::default()).unwrap();

    let pack_phases = Arc::new(Mutex::new(Vec::new()));
    let pc = pack_phases.clone();
    packager
        .build_package(input_dir.path(), &output_path, None, move |e| {
            pc.lock().unwrap().push(e.phase.clone());
        })
        .unwrap();

    let phases = pack_phases.lock().unwrap();
    assert!(phases.contains(&Phase::Scanning));
    assert!(phases.contains(&Phase::Compressing));
    drop(phases);

    let installer = Installer::new(output_path).unwrap();
    let options = InstallOptions {
        install_dir: install_dir.path().to_path_buf(),
        create_shortcuts: false,
        configure_registry: false,
        auto_startup: false,
        components: std::collections::BTreeMap::new(),
        silent: true,
        thread_count: None,
    };

    let inst_phases = Arc::new(Mutex::new(Vec::new()));
    let ic = inst_phases.clone();
    installer
        .install(options, move |e| {
            ic.lock().unwrap().push(e.phase.clone());
        })
        .unwrap();

    let phases = inst_phases.lock().unwrap();
    assert!(phases.contains(&Phase::Decompressing));
    assert!(phases.contains(&Phase::Writing));
}

// 16.2 Uninstall Flow Tests

#[test]
fn test_uninstall_flow_basic() {
    let input_dir = tempdir().unwrap();
    let output_dir = tempdir().unwrap();
    let install_dir = tempdir().unwrap();

    let test_files: Vec<(&str, &[u8])> = vec![
        ("app.exe", b"exe"),
        ("data/cfg.json", b"{}"),
        ("lib/h.dll", b"dll"),
    ];
    create_test_files(input_dir.path(), &test_files);

    let output_path = output_dir.path().join("uninstall.pkg");
    let config = PackagerConfig {
        application_name: "UninstallApp".to_string(),
        ..Default::default()
    };
    Packager::new(config)
        .unwrap()
        .build_package(input_dir.path(), &output_path, None, |_| {})
        .unwrap();

    let installer = Installer::new(output_path).unwrap();
    let options = InstallOptions {
        install_dir: install_dir.path().to_path_buf(),
        create_shortcuts: false,
        configure_registry: false,
        auto_startup: false,
        components: std::collections::BTreeMap::new(),
        silent: true,
        thread_count: None,
    };
    installer.install(options, |_| {}).unwrap();
    installer.create_uninstaller(install_dir.path()).unwrap();

    let manifest = install_dir.path().join("install.manifest.json");
    assert!(manifest.exists());

    for (name, _) in &test_files {
        assert!(install_dir.path().join(name).exists());
    }

    let uninstaller = Uninstaller::from_install_dir(install_dir.path()).unwrap();
    let stats = uninstaller.uninstall(|_| {}).unwrap();

    assert!(stats.files_deleted > 0);
    for (name, _) in &test_files {
        assert!(!install_dir.path().join(name).exists());
    }
    assert!(!manifest.exists());
}

#[test]
fn test_uninstall_removes_empty_directories() {
    let input_dir = tempdir().unwrap();
    let output_dir = tempdir().unwrap();
    let install_dir = tempdir().unwrap();

    let test_files: Vec<(&str, &[u8])> = vec![
        ("l1/l2/l3/deep.txt", b"deep"),
        ("l1/l2/mid.txt", b"mid"),
        ("l1/top.txt", b"top"),
    ];
    create_test_files(input_dir.path(), &test_files);

    let output_path = output_dir.path().join("nested_un.pkg");
    Packager::new(PackagerConfig::default())
        .unwrap()
        .build_package(input_dir.path(), &output_path, None, |_| {})
        .unwrap();

    let installer = Installer::new(output_path).unwrap();
    let options = InstallOptions {
        install_dir: install_dir.path().to_path_buf(),
        create_shortcuts: false,
        configure_registry: false,
        auto_startup: false,
        components: std::collections::BTreeMap::new(),
        silent: true,
        thread_count: None,
    };
    installer.install(options, |_| {}).unwrap();
    installer.create_uninstaller(install_dir.path()).unwrap();

    assert!(install_dir.path().join("l1/l2/l3").is_dir());

    let uninstaller = Uninstaller::from_install_dir(install_dir.path()).unwrap();
    let stats = uninstaller.uninstall(|_| {}).unwrap();

    assert!(stats.directories_removed > 0);
    assert!(!install_dir.path().join("l1").exists());
}

#[test]
fn test_uninstall_handles_missing_files() {
    let input_dir = tempdir().unwrap();
    let output_dir = tempdir().unwrap();
    let install_dir = tempdir().unwrap();

    let test_files: Vec<(&str, &[u8])> = vec![("f1.txt", b"1"), ("f2.txt", b"2"), ("f3.txt", b"3")];
    create_test_files(input_dir.path(), &test_files);

    let output_path = output_dir.path().join("missing.pkg");
    Packager::new(PackagerConfig::default())
        .unwrap()
        .build_package(input_dir.path(), &output_path, None, |_| {})
        .unwrap();

    let installer = Installer::new(output_path).unwrap();
    let options = InstallOptions {
        install_dir: install_dir.path().to_path_buf(),
        create_shortcuts: false,
        configure_registry: false,
        auto_startup: false,
        components: std::collections::BTreeMap::new(),
        silent: true,
        thread_count: None,
    };
    installer.install(options, |_| {}).unwrap();
    installer.create_uninstaller(install_dir.path()).unwrap();

    fs::remove_file(install_dir.path().join("f1.txt")).unwrap();
    fs::remove_file(install_dir.path().join("f2.txt")).unwrap();

    let uninstaller = Uninstaller::from_install_dir(install_dir.path()).unwrap();
    let result = uninstaller.uninstall(|_| {});
    assert!(result.is_ok());
    assert!(!install_dir.path().join("f3.txt").exists());
}

#[test]
fn test_uninstall_progress_events() {
    let input_dir = tempdir().unwrap();
    let output_dir = tempdir().unwrap();
    let install_dir = tempdir().unwrap();

    for i in 0..5 {
        fs::write(
            input_dir.path().join(format!("f{}.txt", i)),
            format!("c{}", i),
        )
        .unwrap();
    }

    let output_path = output_dir.path().join("prog_un.pkg");
    Packager::new(PackagerConfig::default())
        .unwrap()
        .build_package(input_dir.path(), &output_path, None, |_| {})
        .unwrap();

    let installer = Installer::new(output_path).unwrap();
    let options = InstallOptions {
        install_dir: install_dir.path().to_path_buf(),
        create_shortcuts: false,
        configure_registry: false,
        auto_startup: false,
        components: std::collections::BTreeMap::new(),
        silent: true,
        thread_count: None,
    };
    installer.install(options, |_| {}).unwrap();
    installer.create_uninstaller(install_dir.path()).unwrap();

    let uninstaller = Uninstaller::from_install_dir(install_dir.path()).unwrap();
    let count = Arc::new(AtomicUsize::new(0));
    let cc = count.clone();
    uninstaller
        .uninstall(move |_| {
            cc.fetch_add(1, Ordering::SeqCst);
        })
        .unwrap();
    assert!(count.load(Ordering::SeqCst) > 0);
}

// 16.3 Error Handling Tests

#[test]
fn test_error_handling_checksum_verification() {
    let data = b"test data for crc32";
    let crc = calculate_crc32(data);
    assert!(verify_crc32(data, crc).is_ok());
    assert!(verify_crc32(data, crc + 1).is_err());
    assert!(verify_crc32(b"different data", crc).is_err());
}

#[test]
fn test_error_handling_invalid_package() {
    let temp_dir = tempdir().unwrap();
    let invalid_pkg = temp_dir.path().join("invalid.pkg");

    fs::write(&invalid_pkg, b"not a valid package").unwrap();

    // Installer::new only checks file existence, parse_package validates content
    let installer = Installer::new(invalid_pkg).unwrap();
    let result = installer.parse_package();
    assert!(result.is_err());
}

#[test]
fn test_error_handling_package_not_found() {
    let temp_dir = tempdir().unwrap();
    let nonexistent = temp_dir.path().join("nonexistent.pkg");

    let result = Installer::new(nonexistent);
    assert!(result.is_err());
}

#[test]
fn test_error_handling_empty_input_directory() {
    let input_dir = tempdir().unwrap();
    let output_dir = tempdir().unwrap();

    let output_path = output_dir.path().join("empty.pkg");
    let packager = Packager::new(PackagerConfig::default()).unwrap();
    let result = packager.build_package(input_dir.path(), &output_path, None, |_| {});

    match result {
        Ok(stats) => assert_eq!(stats.total_files, 0),
        Err(_) => {}
    }
}

#[test]
fn test_error_handling_disk_space_check() {
    use installer_core::{check_disk_space, get_available_space};

    let temp_dir = tempdir().unwrap();

    let available = get_available_space(temp_dir.path());
    assert!(available.is_ok());
    let space = available.unwrap();
    assert!(space > 0);

    let result = check_disk_space(temp_dir.path(), 1024, 0);
    assert!(result.is_ok());

    let huge_space = u64::MAX / 2;
    let result = check_disk_space(temp_dir.path(), huge_space, 0);
    assert!(result.is_err());
}

#[test]
fn test_error_handling_rollback() {
    let input_dir = tempdir().unwrap();
    let output_dir = tempdir().unwrap();
    let install_dir = tempdir().unwrap();

    let test_files: Vec<(&str, &[u8])> =
        vec![("app.exe", b"exe content"), ("data/config.json", b"{}")];
    create_test_files(input_dir.path(), &test_files);

    let output_path = output_dir.path().join("rollback.pkg");
    Packager::new(PackagerConfig::default())
        .unwrap()
        .build_package(input_dir.path(), &output_path, None, |_| {})
        .unwrap();

    let installer = Installer::new(output_path).unwrap();
    let options = InstallOptions {
        install_dir: install_dir.path().to_path_buf(),
        create_shortcuts: false,
        configure_registry: false,
        auto_startup: false,
        components: std::collections::BTreeMap::new(),
        silent: true,
        thread_count: None,
    };
    installer.install(options, |_| {}).unwrap();

    assert!(install_dir.path().join("app.exe").exists());
    assert!(install_dir.path().join("data/config.json").exists());

    let installed_files: Vec<PathBuf> = vec![
        install_dir.path().join("app.exe"),
        install_dir.path().join("data/config.json"),
    ];
    let result = installer.rollback(&installed_files);
    assert!(result.is_ok());

    assert!(!install_dir.path().join("app.exe").exists());
    assert!(!install_dir.path().join("data/config.json").exists());
}

// 16.4 Concurrency Tests

#[test]
fn test_concurrency_parallel_compression() {
    let input_dir = tempdir().unwrap();
    let output_dir = tempdir().unwrap();

    for i in 0..20 {
        let content: Vec<u8> = (0..10000).map(|j| ((i + j) % 256) as u8).collect();
        fs::write(input_dir.path().join(format!("file_{}.bin", i)), &content).unwrap();
    }

    let output_path = output_dir.path().join("parallel.pkg");
    let config = PackagerConfig {
        block_size: 4096,
        thread_count: Some(4),
        ..Default::default()
    };
    let packager = Packager::new(config).unwrap();

    let events = Arc::new(Mutex::new(Vec::new()));
    let ec = events.clone();
    let stats = packager
        .build_package(input_dir.path(), &output_path, None, move |e| {
            ec.lock().unwrap().push(e.phase.clone());
        })
        .unwrap();

    assert_eq!(stats.total_files, 20);
    assert!(output_path.exists());

    let phases = events.lock().unwrap();
    assert!(phases.contains(&Phase::Compressing));
}

#[test]
fn test_concurrency_parallel_decompression() {
    let input_dir = tempdir().unwrap();
    let output_dir = tempdir().unwrap();
    let install_dir = tempdir().unwrap();

    let mut expected_files = Vec::new();
    for i in 0..15 {
        let content: Vec<u8> = (0..8000).map(|j| ((i * 7 + j) % 256) as u8).collect();
        let name = format!("data_{}.bin", i);
        fs::write(input_dir.path().join(&name), &content).unwrap();
        expected_files.push((name, content));
    }

    let output_path = output_dir.path().join("parallel_decomp.pkg");
    let config = PackagerConfig {
        block_size: 2048,
        thread_count: Some(4),
        ..Default::default()
    };
    Packager::new(config)
        .unwrap()
        .build_package(input_dir.path(), &output_path, None, |_| {})
        .unwrap();

    let installer = Installer::new(output_path).unwrap();
    let options = InstallOptions {
        install_dir: install_dir.path().to_path_buf(),
        create_shortcuts: false,
        configure_registry: false,
        auto_startup: false,
        components: std::collections::BTreeMap::new(),
        silent: true,
        thread_count: Some(4),
    };

    let events = Arc::new(Mutex::new(Vec::new()));
    let ec = events.clone();
    let stats = installer
        .install(options, move |e| {
            ec.lock().unwrap().push(e.phase.clone());
        })
        .unwrap();

    assert_eq!(stats.installed_files, 15);

    for (name, expected_content) in &expected_files {
        let actual = fs::read(install_dir.path().join(name)).unwrap();
        assert_eq!(&actual, expected_content, "File {} content mismatch", name);
    }

    let phases = events.lock().unwrap();
    assert!(phases.contains(&Phase::Decompressing));
}

#[test]
fn test_concurrency_thread_count_configuration() {
    let input_dir = tempdir().unwrap();
    let output_dir = tempdir().unwrap();

    for i in 0..10 {
        fs::write(
            input_dir.path().join(format!("f{}.txt", i)),
            format!("content {}", i),
        )
        .unwrap();
    }

    for thread_count in [1, 2, 4, 8] {
        let output_path = output_dir
            .path()
            .join(format!("threads_{}.pkg", thread_count));
        let config = PackagerConfig {
            thread_count: Some(thread_count),
            ..Default::default()
        };
        let packager = Packager::new(config).unwrap();
        let result = packager.build_package(input_dir.path(), &output_path, None, |_| {});
        assert!(result.is_ok(), "Failed with thread_count={}", thread_count);
        assert!(output_path.exists());
    }
}

#[test]
fn test_concurrency_no_data_races() {
    use std::thread;

    let input_dir = tempdir().unwrap();
    let output_dir = tempdir().unwrap();

    for i in 0..5 {
        let content: Vec<u8> = (0..5000).map(|j| ((i + j) % 256) as u8).collect();
        fs::write(input_dir.path().join(format!("race_{}.bin", i)), &content).unwrap();
    }

    let config = PackagerConfig {
        thread_count: Some(4),
        ..Default::default()
    };

    let handles: Vec<_> = (0..3)
        .map(|run| {
            let input = input_dir.path().to_path_buf();
            let output = output_dir.path().join(format!("race_run_{}.pkg", run));
            let cfg = config.clone();

            thread::spawn(move || {
                let packager = Packager::new(cfg).unwrap();
                packager.build_package(&input, &output, None, |_| {})
            })
        })
        .collect();

    for (i, handle) in handles.into_iter().enumerate() {
        let result = handle.join().expect("Thread panicked");
        assert!(result.is_ok(), "Run {} failed: {:?}", i, result.err());
    }
}

#[test]
fn test_concurrency_progress_events_thread_safe() {
    let input_dir = tempdir().unwrap();
    let output_dir = tempdir().unwrap();

    for i in 0..10 {
        fs::write(
            input_dir.path().join(format!("ts_{}.txt", i)),
            format!("data {}", i),
        )
        .unwrap();
    }

    let output_path = output_dir.path().join("thread_safe.pkg");
    let config = PackagerConfig {
        thread_count: Some(4),
        ..Default::default()
    };
    let packager = Packager::new(config).unwrap();

    let event_count = Arc::new(AtomicUsize::new(0));
    let ec = event_count.clone();

    packager
        .build_package(input_dir.path(), &output_path, None, move |_| {
            ec.fetch_add(1, Ordering::SeqCst);
        })
        .unwrap();

    assert!(event_count.load(Ordering::SeqCst) > 0);
}
