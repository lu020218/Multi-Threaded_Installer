//! Integration tests for the installer system.

use installer_core::{
    Installer, Packager, Uninstaller,
    InstallOptions, PackagerConfig, Phase,
    calculate_crc32, verify_crc32,
};
use std::fs;
use std::path::{Path, PathBuf};
use std::sync::{Arc, Mutex};
use std::sync::atomic::{AtomicUsize, Ordering};
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
        if !file_path.exists() { return false; }
        let actual = fs::read(&file_path).expect("read file");
        if actual != *expected_content { return false; }
    }
    true
}

fn create_ui_resources(dir: &Path) {
    fs::write(dir.join("index.html"), "<html></html>").expect("write html");
    let locales = dir.join("locales");
    fs::create_dir_all(&locales).expect("create locales");
    fs::write(locales.join("en-US.json"), r#"{"k":"v"}"#).expect("write locale");
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
    let stats = packager.build_package(
        input_dir.path(), &output_path, None,
        move |e| { events_clone.lock().unwrap().push(e.phase.clone()); }
    ).unwrap();

    assert!(output_path.exists());
    assert_eq!(stats.total_files, 3);
    assert!(!events.lock().unwrap().is_empty());

    let installer = Installer::new(output_path).unwrap();
    let options = InstallOptions {
        install_dir: install_dir.path().to_path_buf(),
        create_shortcuts: false,
        configure_registry: false,
        auto_startup: false,
        silent: true,
        thread_count: None,
    };
    let install_stats = installer.install(options, |_| {}).unwrap();

    assert_eq!(install_stats.installed_files, 3);
    assert!(verify_installed_files(install_dir.path(), &test_files));
}
#[test]
fn test_complete_install_flow_with_ui_resources() {
    let input_dir = tempdir().unwrap();
    let output_dir = tempdir().unwrap();
    let ui_dir = tempdir().unwrap();
    let install_dir = tempdir().unwrap();

    let test_files: Vec<(&str, &[u8])> = vec![
        ("app.exe", b"exe"),
        ("lib/helper.dll", b"dll"),
    ];
    create_test_files(input_dir.path(), &test_files);
    create_ui_resources(ui_dir.path());

    let output_path = output_dir.path().join("ui.pkg");
    let config = PackagerConfig::default();
    let packager = Packager::new(config).unwrap();
    packager.build_package(input_dir.path(), &output_path, Some(ui_dir.path()), |_| {}).unwrap();

    let installer = Installer::new(output_path.clone()).unwrap();
    assert!(installer.has_ui_resources().unwrap());

    let options = InstallOptions {
        install_dir: install_dir.path().to_path_buf(),
        create_shortcuts: false,
        configure_registry: false,
        auto_startup: false,
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

    let large: Vec<u8> = (0..1024*1024).map(|i| (i % 256) as u8).collect();
    fs::write(input_dir.path().join("large.bin"), &large).unwrap();
    fs::write(input_dir.path().join("small.txt"), b"small").unwrap();

    let output_path = output_dir.path().join("large.pkg");
    let config = PackagerConfig { block_size: 256*1024, ..Default::default() };
    let packager = Packager::new(config).unwrap();
    let stats = packager.build_package(input_dir.path(), &output_path, None, |_| {}).unwrap();
    assert!(stats.total_size >= 1024*1024);

    let installer = Installer::new(output_path).unwrap();
    let options = InstallOptions {
        install_dir: install_dir.path().to_path_buf(),
        create_shortcuts: false,
        configure_registry: false,
        auto_startup: false,
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
    packager.build_package(input_dir.path(), &output_path, None, |_| {}).unwrap();

    let installer = Installer::new(output_path).unwrap();
    let options = InstallOptions {
        install_dir: install_dir.path().to_path_buf(),
        create_shortcuts: false,
        configure_registry: false,
        auto_startup: false,
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
        fs::write(input_dir.path().join(format!("f{}.txt", i)), format!("c{}", i)).unwrap();
    }

    let output_path = output_dir.path().join("progress.pkg");
    let packager = Packager::new(PackagerConfig::default()).unwrap();
    
    let pack_phases = Arc::new(Mutex::new(Vec::new()));
    let pc = pack_phases.clone();
    packager.build_package(input_dir.path(), &output_path, None, move |e| {
        pc.lock().unwrap().push(e.phase.clone());
    }).unwrap();

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
        silent: true,
        thread_count: None,
    };

    let inst_phases = Arc::new(Mutex::new(Vec::new()));
    let ic = inst_phases.clone();
    installer.install(options, move |e| { ic.lock().unwrap().push(e.phase.clone()); }).unwrap();

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
    Packager::new(config).unwrap()
        .build_package(input_dir.path(), &output_path, None, |_| {}).unwrap();

    let installer = Installer::new(output_path).unwrap();
    let options = InstallOptions {
        install_dir: install_dir.path().to_path_buf(),
        create_shortcuts: false,
        configure_registry: false,
        auto_startup: false,
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
    Packager::new(PackagerConfig::default()).unwrap()
        .build_package(input_dir.path(), &output_path, None, |_| {}).unwrap();

    let installer = Installer::new(output_path).unwrap();
    let options = InstallOptions {
        install_dir: install_dir.path().to_path_buf(),
        create_shortcuts: false,
        configure_registry: false,
        auto_startup: false,
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

    let test_files: Vec<(&str, &[u8])> = vec![
        ("f1.txt", b"1"),
        ("f2.txt", b"2"),
        ("f3.txt", b"3"),
    ];
    create_test_files(input_dir.path(), &test_files);

    let output_path = output_dir.path().join("missing.pkg");
    Packager::new(PackagerConfig::default()).unwrap()
        .build_package(input_dir.path(), &output_path, None, |_| {}).unwrap();

    let installer = Installer::new(output_path).unwrap();
    let options = InstallOptions {
        install_dir: install_dir.path().to_path_buf(),
        create_shortcuts: false,
        configure_registry: false,
        auto_startup: false,
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
        fs::write(input_dir.path().join(format!("f{}.txt", i)), format!("c{}", i)).unwrap();
    }

    let output_path = output_dir.path().join("prog_un.pkg");
    Packager::new(PackagerConfig::default()).unwrap()
        .build_package(input_dir.path(), &output_path, None, |_| {}).unwrap();

    let installer = Installer::new(output_path).unwrap();
    let options = InstallOptions {
        install_dir: install_dir.path().to_path_buf(),
        create_shortcuts: false,
        configure_registry: false,
        auto_startup: false,
        silent: true,
        thread_count: None,
    };
    installer.install(options, |_| {}).unwrap();
    installer.create_uninstaller(install_dir.path()).unwrap();

    let uninstaller = Uninstaller::from_install_dir(install_dir.path()).unwrap();
    let count = Arc::new(AtomicUsize::new(0));
    let cc = count.clone();
    uninstaller.uninstall(move |_| { cc.fetch_add(1, Ordering::SeqCst); }).unwrap();
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
    use installer_core::{get_available_space, check_disk_space};
    
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

    let test_files: Vec<(&str, &[u8])> = vec![
        ("app.exe", b"exe content"),
        ("data/config.json", b"{}"),
    ];
    create_test_files(input_dir.path(), &test_files);

    let output_path = output_dir.path().join("rollback.pkg");
    Packager::new(PackagerConfig::default()).unwrap()
        .build_package(input_dir.path(), &output_path, None, |_| {}).unwrap();

    let installer = Installer::new(output_path).unwrap();
    let options = InstallOptions {
        install_dir: install_dir.path().to_path_buf(),
        create_shortcuts: false,
        configure_registry: false,
        auto_startup: false,
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
    let stats = packager.build_package(input_dir.path(), &output_path, None, move |e| {
        ec.lock().unwrap().push(e.phase.clone());
    }).unwrap();

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
    Packager::new(config).unwrap()
        .build_package(input_dir.path(), &output_path, None, |_| {}).unwrap();

    let installer = Installer::new(output_path).unwrap();
    let options = InstallOptions {
        install_dir: install_dir.path().to_path_buf(),
        create_shortcuts: false,
        configure_registry: false,
        auto_startup: false,
        silent: true,
        thread_count: Some(4),
    };
    
    let events = Arc::new(Mutex::new(Vec::new()));
    let ec = events.clone();
    let stats = installer.install(options, move |e| {
        ec.lock().unwrap().push(e.phase.clone());
    }).unwrap();

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
        fs::write(input_dir.path().join(format!("f{}.txt", i)), format!("content {}", i)).unwrap();
    }

    for thread_count in [1, 2, 4, 8] {
        let output_path = output_dir.path().join(format!("threads_{}.pkg", thread_count));
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
    
    let handles: Vec<_> = (0..3).map(|run| {
        let input = input_dir.path().to_path_buf();
        let output = output_dir.path().join(format!("race_run_{}.pkg", run));
        let cfg = config.clone();
        
        thread::spawn(move || {
            let packager = Packager::new(cfg).unwrap();
            packager.build_package(&input, &output, None, |_| {})
        })
    }).collect();

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
        fs::write(input_dir.path().join(format!("ts_{}.txt", i)), format!("data {}", i)).unwrap();
    }

    let output_path = output_dir.path().join("thread_safe.pkg");
    let config = PackagerConfig {
        thread_count: Some(4),
        ..Default::default()
    };
    let packager = Packager::new(config).unwrap();
    
    let event_count = Arc::new(AtomicUsize::new(0));
    let ec = event_count.clone();
    
    packager.build_package(input_dir.path(), &output_path, None, move |_| {
        ec.fetch_add(1, Ordering::SeqCst);
    }).unwrap();

    assert!(event_count.load(Ordering::SeqCst) > 0);
}
