//! Configuration models for the packager and installer.

use serde::{Deserialize, Serialize};
use std::path::PathBuf;

use crate::format::{CompressionAlgorithm, RegistryEntry, WindowsVersion};

/// Packager configuration loaded from packager.json.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct PackagerConfig {
    /// Application name
    pub application_name: String,
    /// Application version
    pub version: String,
    /// Default installation directory (supports environment variables)
    pub default_install_dir: String,
    /// Vendor/publisher name
    #[serde(default)]
    pub vendor: Option<String>,
    /// License text or path to license file
    #[serde(default)]
    pub license_text: Option<String>,
    /// Path to icon file
    #[serde(default)]
    pub icon_path: Option<String>,
    /// Compression algorithm to use
    #[serde(default)]
    pub compression_algorithm: CompressionAlgorithm,
    /// Compression level (1-22 for Zstd, 0-9 for LZMA)
    #[serde(default = "default_compression_level")]
    pub compression_level: u8,
    /// Block size in bytes (default: 4MB)
    #[serde(default = "default_block_size")]
    pub block_size: usize,
    /// Custom folder targets for installation
    #[serde(default)]
    pub folder_targets: Vec<FolderTarget>,
    /// Custom registry entries
    #[serde(default)]
    pub registry_entries: Vec<RegistryEntry>,
    /// Whether admin privileges are required
    #[serde(default)]
    pub require_admin: bool,
    /// Enable auto-startup
    #[serde(default)]
    pub auto_startup: bool,
    /// Create desktop icons
    #[serde(default)]
    pub desktop_icons: bool,
    /// Minimum Windows version requirement
    #[serde(default)]
    pub min_windows_version: Option<WindowsVersion>,
    /// Process name to check before installation
    #[serde(default)]
    pub process_name: Option<String>,
    /// Path to UI resources directory
    #[serde(default)]
    pub ui_resources_dir: Option<PathBuf>,
    /// Number of threads for parallel operations (default: CPU count)
    #[serde(default)]
    pub thread_count: Option<usize>,
}

fn default_compression_level() -> u8 {
    3
}

fn default_block_size() -> usize {
    4 * 1024 * 1024 // 4MB
}

impl Default for PackagerConfig {
    fn default() -> Self {
        Self {
            application_name: String::from("MyApp"),
            version: String::from("1.0.0"),
            default_install_dir: String::from("%ProgramFiles%"),
            vendor: None,
            license_text: None,
            icon_path: None,
            compression_algorithm: CompressionAlgorithm::Zstd,
            compression_level: default_compression_level(),
            block_size: default_block_size(),
            folder_targets: Vec::new(),
            registry_entries: Vec::new(),
            require_admin: false,
            auto_startup: false,
            desktop_icons: false,
            min_windows_version: None,
            process_name: None,
            ui_resources_dir: None,
            thread_count: None,
        }
    }
}

/// Custom folder target for installation.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct FolderTarget {
    /// Source folder name (relative to input directory)
    pub folder_name: String,
    /// Target directory (supports environment variables)
    pub target_directory: String,
}

/// Installation options for the installer.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct InstallOptions {
    /// Installation directory
    pub install_dir: PathBuf,
    /// Create desktop shortcuts
    pub create_shortcuts: bool,
    /// Configure registry entries
    pub configure_registry: bool,
    /// Enable auto-startup
    pub auto_startup: bool,
    /// Silent installation (no UI prompts)
    pub silent: bool,
    /// Number of threads for parallel operations
    pub thread_count: Option<usize>,
}

impl Default for InstallOptions {
    fn default() -> Self {
        Self {
            install_dir: PathBuf::new(),
            create_shortcuts: true,
            configure_registry: true,
            auto_startup: false,
            silent: false,
            thread_count: None,
        }
    }
}

/// Localization configuration.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct LocalizationConfig {
    /// Default locale (e.g., "zh-CN")
    pub default_locale: String,
    /// Fallback locale (e.g., "en-US")
    pub fallback_locale: String,
    /// List of supported locales
    pub supported_locales: Vec<String>,
}

impl Default for LocalizationConfig {
    fn default() -> Self {
        Self {
            default_locale: String::from("en-US"),
            fallback_locale: String::from("en-US"),
            supported_locales: vec![String::from("en-US"), String::from("zh-CN")],
        }
    }
}


#[cfg(test)]
mod tests {
    use super::*;
    use crate::format::RegistryValueType;

    #[test]
    fn test_packager_config_default() {
        let config = PackagerConfig::default();
        assert_eq!(config.application_name, "MyApp");
        assert_eq!(config.version, "1.0.0");
        assert_eq!(config.compression_level, 3);
        assert_eq!(config.block_size, 4 * 1024 * 1024);
    }

    #[test]
    fn test_packager_config_json_roundtrip() {
        let config = PackagerConfig {
            application_name: "TestApp".to_string(),
            version: "2.0.0".to_string(),
            default_install_dir: "%ProgramFiles%\\TestApp".to_string(),
            vendor: Some("Test Vendor".to_string()),
            license_text: Some("MIT License".to_string()),
            icon_path: Some("icon.ico".to_string()),
            compression_algorithm: CompressionAlgorithm::Zstd,
            compression_level: 5,
            block_size: 2 * 1024 * 1024,
            folder_targets: vec![FolderTarget {
                folder_name: "src".to_string(),
                target_directory: "bin".to_string(),
            }],
            registry_entries: vec![RegistryEntry {
                path: "HKEY_CURRENT_USER\\Software\\TestApp".to_string(),
                key: "Version".to_string(),
                value: "2.0.0".to_string(),
                value_type: RegistryValueType::String,
            }],
            require_admin: true,
            auto_startup: true,
            desktop_icons: true,
            min_windows_version: Some(WindowsVersion {
                major: 10,
                minor: 0,
                build: 19041,
            }),
            process_name: Some("testapp.exe".to_string()),
            ui_resources_dir: Some(PathBuf::from("ui")),
            thread_count: Some(4),
        };

        // Serialize to JSON
        let json = serde_json::to_string(&config).unwrap();
        
        // Deserialize from JSON
        let deserialized: PackagerConfig = serde_json::from_str(&json).unwrap();
        
        assert_eq!(config, deserialized);
    }

    #[test]
    fn test_packager_config_messagepack_roundtrip() {
        let config = PackagerConfig {
            application_name: "TestApp".to_string(),
            version: "1.0.0".to_string(),
            ..Default::default()
        };

        // Serialize to MessagePack
        let msgpack = rmp_serde::to_vec(&config).unwrap();
        
        // Deserialize from MessagePack
        let deserialized: PackagerConfig = rmp_serde::from_slice(&msgpack).unwrap();
        
        assert_eq!(config, deserialized);
    }

    #[test]
    fn test_folder_target_serialization() {
        let target = FolderTarget {
            folder_name: "src".to_string(),
            target_directory: "bin".to_string(),
        };

        let json = serde_json::to_string(&target).unwrap();
        let deserialized: FolderTarget = serde_json::from_str(&json).unwrap();
        
        assert_eq!(target, deserialized);
    }

    #[test]
    fn test_localization_config_default() {
        let config = LocalizationConfig::default();
        assert_eq!(config.default_locale, "en-US");
        assert_eq!(config.fallback_locale, "en-US");
        assert!(config.supported_locales.contains(&"en-US".to_string()));
        assert!(config.supported_locales.contains(&"zh-CN".to_string()));
    }
}

// ============================================================================
// Property-Based Tests
// ============================================================================

#[cfg(test)]
mod property_tests {
    use super::*;
    use crate::format::{CompressionAlgorithm, RegistryValueType};
    use proptest::prelude::*;

    /// Strategy for generating valid application names (non-empty strings without null bytes)
    fn app_name_strategy() -> impl Strategy<Value = String> {
        "[a-zA-Z][a-zA-Z0-9_-]{0,30}".prop_map(|s| s)
    }

    /// Strategy for generating valid version strings
    fn version_strategy() -> impl Strategy<Value = String> {
        (1u32..100, 0u32..100, 0u32..1000).prop_map(|(major, minor, patch)| {
            format!("{}.{}.{}", major, minor, patch)
        })
    }

    /// Strategy for generating valid directory paths
    fn dir_path_strategy() -> impl Strategy<Value = String> {
        prop::collection::vec("[a-zA-Z0-9_-]{1,10}", 1..4)
            .prop_map(|parts| parts.join("\\"))
    }

    /// Strategy for generating optional strings
    fn optional_string_strategy() -> impl Strategy<Value = Option<String>> {
        prop::option::of("[a-zA-Z0-9_ ]{1,50}")
    }

    /// Strategy for generating compression algorithms
    fn compression_algorithm_strategy() -> impl Strategy<Value = CompressionAlgorithm> {
        prop_oneof![
            Just(CompressionAlgorithm::Zstd),
            Just(CompressionAlgorithm::Lzma),
        ]
    }

    /// Strategy for generating compression levels (valid range)
    fn compression_level_strategy() -> impl Strategy<Value = u8> {
        1u8..22
    }

    /// Strategy for generating block sizes (reasonable range)
    fn block_size_strategy() -> impl Strategy<Value = usize> {
        1024usize..16 * 1024 * 1024
    }

    /// Strategy for generating folder targets
    fn folder_target_strategy() -> impl Strategy<Value = FolderTarget> {
        ("[a-zA-Z][a-zA-Z0-9_]{0,15}", "[a-zA-Z][a-zA-Z0-9_]{0,15}").prop_map(
            |(folder_name, target_directory)| FolderTarget {
                folder_name,
                target_directory,
            },
        )
    }

    /// Strategy for generating registry value types
    fn registry_value_type_strategy() -> impl Strategy<Value = RegistryValueType> {
        prop_oneof![
            Just(RegistryValueType::String),
            Just(RegistryValueType::Dword),
            Just(RegistryValueType::ExpandString),
        ]
    }

    /// Strategy for generating registry entries
    fn registry_entry_strategy() -> impl Strategy<Value = crate::format::RegistryEntry> {
        (
            "HKEY_[A-Z_]+\\\\[a-zA-Z0-9_\\\\]{1,30}",
            "[a-zA-Z][a-zA-Z0-9_]{0,15}",
            "[a-zA-Z0-9_ ]{1,30}",
            registry_value_type_strategy(),
        )
            .prop_map(|(path, key, value, value_type)| crate::format::RegistryEntry {
                path,
                key,
                value,
                value_type,
            })
    }

    /// Strategy for generating Windows versions
    fn windows_version_strategy() -> impl Strategy<Value = Option<WindowsVersion>> {
        prop::option::of((6u16..11, 0u16..10, 10000u32..30000).prop_map(
            |(major, minor, build)| WindowsVersion {
                major,
                minor,
                build,
            },
        ))
    }

    /// Strategy for generating complete PackagerConfig
    /// Uses nested tuples to work around proptest's 12-element tuple limit
    fn packager_config_strategy() -> impl Strategy<Value = PackagerConfig> {
        // First group: basic info (6 elements)
        let basic_info = (
            app_name_strategy(),
            version_strategy(),
            dir_path_strategy(),
            optional_string_strategy(),
            optional_string_strategy(),
            optional_string_strategy(),
        );
        
        // Second group: compression settings (3 elements)
        let compression_settings = (
            compression_algorithm_strategy(),
            compression_level_strategy(),
            block_size_strategy(),
        );
        
        // Third group: collections (2 elements)
        let collections = (
            prop::collection::vec(folder_target_strategy(), 0..3),
            prop::collection::vec(registry_entry_strategy(), 0..3),
        );
        
        // Fourth group: boolean flags (3 elements)
        let bool_flags = (any::<bool>(), any::<bool>(), any::<bool>());
        
        // Fifth group: optional fields (3 elements)
        let optional_fields = (
            windows_version_strategy(),
            optional_string_strategy(),
            prop::option::of(1usize..16),
        );
        
        // Combine all groups
        (basic_info, compression_settings, collections, bool_flags, optional_fields).prop_map(
            |(
                (application_name, version, default_install_dir, vendor, license_text, icon_path),
                (compression_algorithm, compression_level, block_size),
                (folder_targets, registry_entries),
                (require_admin, auto_startup, desktop_icons),
                (min_windows_version, process_name, thread_count),
            )| {
                PackagerConfig {
                    application_name,
                    version,
                    default_install_dir,
                    vendor,
                    license_text,
                    icon_path,
                    compression_algorithm,
                    compression_level,
                    block_size,
                    folder_targets,
                    registry_entries,
                    require_admin,
                    auto_startup,
                    desktop_icons,
                    min_windows_version,
                    process_name,
                    ui_resources_dir: None, // PathBuf doesn't implement Arbitrary
                    thread_count,
                }
            },
        )
    }

    proptest! {
        #![proptest_config(ProptestConfig::with_cases(100))]

        /// **Property 2: Configuration Serialization Round-Trip (JSON)**
        /// For any valid PackagerConfig object, serializing to JSON and then
        /// deserializing should produce an equivalent configuration object.
        ///
        /// **Validates: Requirements 2.7, 9.2**
        #[test]
        fn prop_config_json_roundtrip(config in packager_config_strategy()) {
            // Serialize to JSON
            let json = serde_json::to_string(&config)
                .expect("Failed to serialize config to JSON");
            
            // Deserialize from JSON
            let deserialized: PackagerConfig = serde_json::from_str(&json)
                .expect("Failed to deserialize config from JSON");
            
            // Property: Deserialized config should equal original
            prop_assert_eq!(
                config,
                deserialized,
                "JSON round-trip should preserve all config fields"
            );
        }

        /// **Property 2: Configuration Serialization Round-Trip (MessagePack)**
        /// For any valid PackagerConfig object, serializing to MessagePack and then
        /// deserializing should produce an equivalent configuration object.
        ///
        /// **Validates: Requirements 2.7, 9.2**
        #[test]
        fn prop_config_messagepack_roundtrip(config in packager_config_strategy()) {
            // Serialize to MessagePack
            let msgpack = rmp_serde::to_vec(&config)
                .expect("Failed to serialize config to MessagePack");
            
            // Deserialize from MessagePack
            let deserialized: PackagerConfig = rmp_serde::from_slice(&msgpack)
                .expect("Failed to deserialize config from MessagePack");
            
            // Property: Deserialized config should equal original
            prop_assert_eq!(
                config,
                deserialized,
                "MessagePack round-trip should preserve all config fields"
            );
        }

        /// **Property 2 (continued): JSON and MessagePack Equivalence**
        /// Serializing to JSON, then to MessagePack, and back should produce
        /// the same result as direct MessagePack round-trip.
        ///
        /// **Validates: Requirements 2.7, 9.2**
        #[test]
        fn prop_config_json_msgpack_equivalence(config in packager_config_strategy()) {
            // JSON round-trip
            let json = serde_json::to_string(&config).unwrap();
            let from_json: PackagerConfig = serde_json::from_str(&json).unwrap();
            
            // MessagePack round-trip
            let msgpack = rmp_serde::to_vec(&config).unwrap();
            let from_msgpack: PackagerConfig = rmp_serde::from_slice(&msgpack).unwrap();
            
            // Property: Both should produce the same result
            prop_assert_eq!(
                from_json,
                from_msgpack,
                "JSON and MessagePack round-trips should produce equivalent results"
            );
        }

        /// **Property 2 (continued): FolderTarget Serialization Round-Trip**
        /// For any valid FolderTarget, serialization round-trip should preserve all fields.
        ///
        /// **Validates: Requirements 9.8**
        #[test]
        fn prop_folder_target_roundtrip(target in folder_target_strategy()) {
            // JSON round-trip
            let json = serde_json::to_string(&target).unwrap();
            let from_json: FolderTarget = serde_json::from_str(&json).unwrap();
            
            prop_assert_eq!(target, from_json, "FolderTarget JSON round-trip should preserve all fields");
        }

        /// **Property 2 (continued): RegistryEntry Serialization Round-Trip**
        /// For any valid RegistryEntry, serialization round-trip should preserve all fields.
        ///
        /// **Validates: Requirements 9.9**
        #[test]
        fn prop_registry_entry_roundtrip(entry in registry_entry_strategy()) {
            // JSON round-trip
            let json = serde_json::to_string(&entry).unwrap();
            let from_json: crate::format::RegistryEntry = serde_json::from_str(&json).unwrap();
            
            prop_assert_eq!(entry, from_json, "RegistryEntry JSON round-trip should preserve all fields");
        }

        /// **Property 2 (continued): LocalizationConfig Serialization Round-Trip**
        /// For any valid LocalizationConfig, serialization round-trip should preserve all fields.
        #[test]
        fn prop_localization_config_roundtrip(
            default_locale in "[a-z]{2}-[A-Z]{2}",
            fallback_locale in "[a-z]{2}-[A-Z]{2}",
            supported_locales in prop::collection::vec("[a-z]{2}-[A-Z]{2}", 1..5)
        ) {
            let config = LocalizationConfig {
                default_locale,
                fallback_locale,
                supported_locales,
            };
            
            // JSON round-trip
            let json = serde_json::to_string(&config).unwrap();
            let from_json: LocalizationConfig = serde_json::from_str(&json).unwrap();
            
            prop_assert_eq!(config, from_json, "LocalizationConfig JSON round-trip should preserve all fields");
        }
    }
}
