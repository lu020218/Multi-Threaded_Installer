//! Multi-language support module.
//!
//! This module provides:
//! - LocalizationManager for loading and managing translations
//! - System locale detection
//! - Variable interpolation in translation strings
//! - Fallback to default language when translations are missing

use installer_shared::{InstallerError, LocalizationConfig, Result};
use std::collections::HashMap;
use std::path::Path;
use tracing::{debug, warn};

/// Manager for handling multi-language translations.
#[derive(Debug, Clone)]
pub struct LocalizationManager {
    /// Configuration for localization
    config: LocalizationConfig,
    /// Translations map: locale -> (key -> value)
    translations: HashMap<String, HashMap<String, String>>,
    /// Current active locale
    current_locale: String,
}

impl LocalizationManager {
    /// Create a new LocalizationManager with the given configuration.
    pub fn new(config: LocalizationConfig) -> Self {
        let current_locale = config.default_locale.clone();
        Self {
            config,
            translations: HashMap::new(),
            current_locale,
        }
    }

    /// Create a LocalizationManager with default configuration.
    pub fn with_defaults() -> Self {
        Self::new(LocalizationConfig::default())
    }

    /// Load translations from a resources directory.
    ///
    /// The directory should contain a `locales` subdirectory with JSON files
    /// named after the locale (e.g., `en-US.json`, `zh-CN.json`).
    pub fn load_from_resources(&mut self, resources_dir: &Path) -> Result<()> {
        let locales_dir = resources_dir.join("locales");

        if !locales_dir.exists() {
            return Err(InstallerError::Config(format!(
                "Locales directory not found: {}",
                locales_dir.display()
            )));
        }

        debug!("Loading translations from: {}", locales_dir.display());

        for locale in &self.config.supported_locales {
            let locale_file = locales_dir.join(format!("{}.json", locale));

            if locale_file.exists() {
                match self.load_locale_file(&locale_file, locale) {
                    Ok(translations) => {
                        debug!(
                            "Loaded {} translations for locale '{}'",
                            translations.len(),
                            locale
                        );
                        self.translations.insert(locale.clone(), translations);
                    }
                    Err(e) => {
                        warn!("Failed to load locale '{}': {}", locale, e);
                        // Continue loading other locales
                    }
                }
            } else {
                warn!("Locale file not found: {}", locale_file.display());
            }
        }

        // Ensure at least the fallback locale is loaded
        if !self.translations.contains_key(&self.config.fallback_locale) {
            return Err(InstallerError::Config(format!(
                "Fallback locale '{}' not loaded",
                self.config.fallback_locale
            )));
        }

        Ok(())
    }

    /// Load a single locale file and return the translations.
    fn load_locale_file(&self, path: &Path, locale: &str) -> Result<HashMap<String, String>> {
        let content = std::fs::read_to_string(path).map_err(|e| InstallerError::Io(e))?;

        let translations: HashMap<String, String> =
            serde_json::from_str(&content).map_err(|e| {
                InstallerError::Config(format!("Failed to parse locale file '{}': {}", locale, e))
            })?;

        Ok(translations)
    }

    /// Load translations directly from a HashMap (useful for testing or embedded resources).
    pub fn load_translations(&mut self, locale: &str, translations: HashMap<String, String>) {
        self.translations.insert(locale.to_string(), translations);
    }

    /// Get translated text for a key with optional variable interpolation.
    ///
    /// Variables in the translation string are specified as `{variableName}`.
    /// If the key is not found in the current locale, it falls back to the
    /// fallback locale. If still not found, returns the key itself.
    pub fn get_text(&self, key: &str) -> String {
        self.get_text_with_vars(key, &HashMap::new())
    }

    /// Get translated text with variable interpolation.
    ///
    /// # Arguments
    /// * `key` - The translation key
    /// * `vars` - A map of variable names to their values
    ///
    /// # Example
    /// ```ignore
    /// let mut vars = HashMap::new();
    /// vars.insert("appName".to_string(), "MyApp".to_string());
    /// let text = manager.get_text_with_vars("welcome.description", &vars);
    /// // Returns: "This will install MyApp on your computer."
    /// ```
    pub fn get_text_with_vars(&self, key: &str, vars: &HashMap<String, String>) -> String {
        let text = self.get_raw_text(key);
        self.interpolate_vars(&text, vars)
    }

    /// Get the raw translation text without variable interpolation.
    fn get_raw_text(&self, key: &str) -> String {
        // Try current locale first
        if let Some(translations) = self.translations.get(&self.current_locale) {
            if let Some(text) = translations.get(key) {
                return text.clone();
            }
        }

        // Fall back to fallback locale
        if self.current_locale != self.config.fallback_locale {
            if let Some(translations) = self.translations.get(&self.config.fallback_locale) {
                if let Some(text) = translations.get(key) {
                    warn!(
                        "Translation key '{}' not found in locale '{}', using fallback '{}'",
                        key, self.current_locale, self.config.fallback_locale
                    );
                    return text.clone();
                }
            }
        }

        // Key not found anywhere, log and return the key itself
        warn!(
            "Translation key '{}' not found in any locale, returning key",
            key
        );
        key.to_string()
    }

    /// Interpolate variables in a text string.
    ///
    /// Variables are specified as `{variableName}` in the text.
    fn interpolate_vars(&self, text: &str, vars: &HashMap<String, String>) -> String {
        let mut result = text.to_string();

        for (name, value) in vars {
            let placeholder = format!("{{{}}}", name);
            result = result.replace(&placeholder, value);
        }

        result
    }

    /// Detect the system locale.
    ///
    /// On Windows, uses GetUserDefaultLocaleName.
    /// Returns the detected locale or the fallback locale if detection fails.
    pub fn detect_system_locale() -> String {
        #[cfg(windows)]
        {
            Self::detect_windows_locale()
        }

        #[cfg(not(windows))]
        {
            // On non-Windows platforms, try environment variables
            std::env::var("LANG")
                .or_else(|_| std::env::var("LC_ALL"))
                .or_else(|_| std::env::var("LC_MESSAGES"))
                .map(|lang| {
                    // Parse locale string (e.g., "en_US.UTF-8" -> "en-US")
                    let lang = lang.split('.').next().unwrap_or(&lang);
                    lang.replace('_', "-")
                })
                .unwrap_or_else(|_| "en-US".to_string())
        }
    }

    /// Detect locale on Windows using GetUserDefaultLocaleName.
    #[cfg(windows)]
    fn detect_windows_locale() -> String {
        use std::ffi::OsString;
        use std::os::windows::ffi::OsStringExt;

        // LOCALE_NAME_MAX_LENGTH is 85
        const LOCALE_NAME_MAX_LENGTH: usize = 85;
        let mut buffer: Vec<u16> = vec![0; LOCALE_NAME_MAX_LENGTH];

        let len = unsafe {
            windows_sys::Win32::Globalization::GetUserDefaultLocaleName(
                buffer.as_mut_ptr(),
                buffer.len() as i32,
            )
        };

        if len > 0 {
            // Remove null terminator
            buffer.truncate((len - 1) as usize);
            let locale = OsString::from_wide(&buffer);
            locale.to_string_lossy().to_string()
        } else {
            debug!("Failed to detect Windows locale, using default");
            "en-US".to_string()
        }
    }

    /// Set the current locale.
    ///
    /// Returns an error if the locale is not supported.
    pub fn set_locale(&mut self, locale: &str) -> Result<()> {
        if !self.config.supported_locales.contains(&locale.to_string()) {
            return Err(InstallerError::Config(format!(
                "Locale '{}' is not supported. Supported locales: {:?}",
                locale, self.config.supported_locales
            )));
        }

        if !self.translations.contains_key(locale) {
            return Err(InstallerError::Config(format!(
                "Translations for locale '{}' are not loaded",
                locale
            )));
        }

        debug!("Setting locale to '{}'", locale);
        self.current_locale = locale.to_string();
        Ok(())
    }

    /// Get the current locale.
    pub fn current_locale(&self) -> &str {
        &self.current_locale
    }

    /// Get the list of supported locales.
    pub fn supported_locales(&self) -> &[String] {
        &self.config.supported_locales
    }

    /// Get the list of loaded locales.
    pub fn loaded_locales(&self) -> Vec<&String> {
        self.translations.keys().collect()
    }

    /// Check if a translation key exists in the current locale.
    pub fn has_key(&self, key: &str) -> bool {
        if let Some(translations) = self.translations.get(&self.current_locale) {
            return translations.contains_key(key);
        }
        false
    }

    /// Check if a translation key exists in any loaded locale.
    pub fn has_key_in_any_locale(&self, key: &str) -> bool {
        for translations in self.translations.values() {
            if translations.contains_key(key) {
                return true;
            }
        }
        false
    }

    /// Get all translation keys from the fallback locale.
    pub fn all_keys(&self) -> Vec<&String> {
        if let Some(translations) = self.translations.get(&self.config.fallback_locale) {
            translations.keys().collect()
        } else {
            Vec::new()
        }
    }

    /// Check translation completeness for a locale.
    ///
    /// Returns a list of keys that are missing in the specified locale
    /// compared to the fallback locale.
    pub fn missing_keys(&self, locale: &str) -> Vec<String> {
        let fallback_keys: std::collections::HashSet<_> = self
            .translations
            .get(&self.config.fallback_locale)
            .map(|t| t.keys().collect())
            .unwrap_or_default();

        let locale_keys: std::collections::HashSet<_> = self
            .translations
            .get(locale)
            .map(|t| t.keys().collect())
            .unwrap_or_default();

        fallback_keys
            .difference(&locale_keys)
            .map(|k| (*k).clone())
            .collect()
    }

    /// Get the configuration.
    pub fn config(&self) -> &LocalizationConfig {
        &self.config
    }
}

impl Default for LocalizationManager {
    fn default() -> Self {
        Self::with_defaults()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn create_test_manager() -> LocalizationManager {
        let mut manager = LocalizationManager::with_defaults();

        // Load English translations
        let mut en_translations = HashMap::new();
        en_translations.insert("welcome.title".to_string(), "Welcome".to_string());
        en_translations.insert(
            "welcome.description".to_string(),
            "This will install {appName} on your computer.".to_string(),
        );
        en_translations.insert("button.next".to_string(), "Next".to_string());
        en_translations.insert("button.cancel".to_string(), "Cancel".to_string());
        manager.load_translations("en-US", en_translations);

        // Load Chinese translations
        let mut zh_translations = HashMap::new();
        zh_translations.insert("welcome.title".to_string(), "欢迎".to_string());
        zh_translations.insert(
            "welcome.description".to_string(),
            "这将在您的计算机上安装 {appName}。".to_string(),
        );
        zh_translations.insert("button.next".to_string(), "下一步".to_string());
        // Note: button.cancel is intentionally missing to test fallback
        manager.load_translations("zh-CN", zh_translations);

        manager
    }

    #[test]
    fn test_get_text_basic() {
        let manager = create_test_manager();
        assert_eq!(manager.get_text("welcome.title"), "Welcome");
        assert_eq!(manager.get_text("button.next"), "Next");
    }

    #[test]
    fn test_get_text_with_vars() {
        let manager = create_test_manager();
        let mut vars = HashMap::new();
        vars.insert("appName".to_string(), "TestApp".to_string());

        let text = manager.get_text_with_vars("welcome.description", &vars);
        assert_eq!(text, "This will install TestApp on your computer.");
    }

    #[test]
    fn test_locale_switching() {
        let mut manager = create_test_manager();

        // Default is en-US
        assert_eq!(manager.get_text("welcome.title"), "Welcome");

        // Switch to zh-CN
        manager.set_locale("zh-CN").unwrap();
        assert_eq!(manager.get_text("welcome.title"), "欢迎");
    }

    #[test]
    fn test_fallback_on_missing_key() {
        let mut manager = create_test_manager();
        manager.set_locale("zh-CN").unwrap();

        // button.cancel is missing in zh-CN, should fall back to en-US
        assert_eq!(manager.get_text("button.cancel"), "Cancel");
    }

    #[test]
    fn test_missing_key_returns_key() {
        let manager = create_test_manager();

        // Non-existent key should return the key itself
        assert_eq!(manager.get_text("nonexistent.key"), "nonexistent.key");
    }

    #[test]
    fn test_variable_interpolation_multiple_vars() {
        let mut manager = LocalizationManager::with_defaults();
        let mut translations = HashMap::new();
        translations.insert(
            "message".to_string(),
            "Hello {name}, you have {count} messages.".to_string(),
        );
        manager.load_translations("en-US", translations);

        let mut vars = HashMap::new();
        vars.insert("name".to_string(), "Alice".to_string());
        vars.insert("count".to_string(), "5".to_string());

        let text = manager.get_text_with_vars("message", &vars);
        assert_eq!(text, "Hello Alice, you have 5 messages.");
    }

    #[test]
    fn test_missing_keys_detection() {
        let manager = create_test_manager();

        let missing = manager.missing_keys("zh-CN");
        assert!(missing.contains(&"button.cancel".to_string()));
    }

    #[test]
    fn test_has_key() {
        let manager = create_test_manager();

        assert!(manager.has_key("welcome.title"));
        assert!(!manager.has_key("nonexistent.key"));
    }

    #[test]
    fn test_unsupported_locale() {
        let mut manager = create_test_manager();

        let result = manager.set_locale("fr-FR");
        assert!(result.is_err());
    }

    #[test]
    fn test_detect_system_locale() {
        // Just ensure it doesn't panic and returns a valid string
        let locale = LocalizationManager::detect_system_locale();
        assert!(!locale.is_empty());
    }
}

#[cfg(test)]
mod property_tests {
    use super::*;
    use proptest::collection::hash_map;
    use proptest::prelude::*;

    /// Generate a valid translation key (alphanumeric with dots)
    fn arb_translation_key() -> impl Strategy<Value = String> {
        prop::string::string_regex("[a-z][a-z0-9]*\\.[a-z][a-z0-9]*")
            .unwrap()
            .prop_filter("key must not be empty", |s| !s.is_empty())
    }

    /// Generate a valid translation value (non-empty string with optional variables)
    fn arb_translation_value() -> impl Strategy<Value = String> {
        prop::string::string_regex("[A-Za-z0-9 ,.!?]+")
            .unwrap()
            .prop_filter("value must not be empty", |s| !s.is_empty())
    }

    /// Generate a set of translations (key -> value map)
    fn arb_translations() -> impl Strategy<Value = HashMap<String, String>> {
        hash_map(arb_translation_key(), arb_translation_value(), 1..10)
    }

    proptest! {
        #![proptest_config(ProptestConfig::with_cases(100))]

        /// Property 21: Multi-language Translation Completeness
        /// For any supported language, all UI text keys should have a corresponding
        /// translation. If missing, it should fall back to the default language.
        /// **Validates: Requirements UI多语言支持**
        #[test]
        fn prop_translation_completeness_fallback(
            fallback_translations in arb_translations(),
            locale_translations in arb_translations(),
        ) {
            let config = LocalizationConfig {
                default_locale: "en-US".to_string(),
                fallback_locale: "en-US".to_string(),
                supported_locales: vec!["en-US".to_string(), "zh-CN".to_string()],
            };

            let mut manager = LocalizationManager::new(config);

            // Load fallback locale translations
            manager.load_translations("en-US", fallback_translations.clone());

            // Load secondary locale with potentially missing keys
            manager.load_translations("zh-CN", locale_translations.clone());

            // Switch to secondary locale
            manager.set_locale("zh-CN").unwrap();

            // Property: For every key in the fallback locale, get_text should return
            // either the translation from zh-CN or fall back to en-US
            for (key, fallback_value) in &fallback_translations {
                let result = manager.get_text(key);

                // Result should either be from zh-CN or fallback to en-US
                if let Some(zh_value) = locale_translations.get(key) {
                    // If zh-CN has the key, it should return that value
                    prop_assert_eq!(&result, zh_value,
                        "Key '{}' exists in zh-CN but got wrong value", key);
                } else {
                    // If zh-CN doesn't have the key, it should fall back to en-US
                    prop_assert_eq!(&result, fallback_value,
                        "Key '{}' missing in zh-CN should fall back to en-US", key);
                }
            }
        }

        /// Property: Variable interpolation should work correctly for any valid variable
        #[test]
        fn prop_variable_interpolation(
            base_text in arb_translation_value(),
            var_name in "[a-zA-Z][a-zA-Z0-9]{0,10}",
            var_value in arb_translation_value(),
        ) {
            let mut manager = LocalizationManager::with_defaults();

            // Create a translation with a variable placeholder
            let template = format!("{} {{{}}} end", base_text, var_name);
            let mut translations = HashMap::new();
            translations.insert("test.key".to_string(), template.clone());
            manager.load_translations("en-US", translations);

            // Interpolate the variable
            let mut vars = HashMap::new();
            vars.insert(var_name.clone(), var_value.clone());

            let result = manager.get_text_with_vars("test.key", &vars);

            // The result should have the variable replaced
            let expected = format!("{} {} end", base_text, var_value);
            prop_assert_eq!(&result, &expected);

            // The result should not contain the placeholder
            prop_assert!(!result.contains(&format!("{{{}}}", var_name)),
                "Result should not contain placeholder: {}", result);
        }

        /// Property: Missing keys detection should be accurate
        #[test]
        fn prop_missing_keys_detection(
            fallback_translations in arb_translations(),
            locale_translations in arb_translations(),
        ) {
            let config = LocalizationConfig {
                default_locale: "en-US".to_string(),
                fallback_locale: "en-US".to_string(),
                supported_locales: vec!["en-US".to_string(), "zh-CN".to_string()],
            };

            let mut manager = LocalizationManager::new(config);
            manager.load_translations("en-US", fallback_translations.clone());
            manager.load_translations("zh-CN", locale_translations.clone());

            let missing = manager.missing_keys("zh-CN");

            // Every key in fallback that's not in locale should be in missing
            for key in fallback_translations.keys() {
                if !locale_translations.contains_key(key) {
                    prop_assert!(missing.contains(key),
                        "Key '{}' should be in missing keys", key);
                } else {
                    prop_assert!(!missing.contains(key),
                        "Key '{}' should not be in missing keys", key);
                }
            }
        }

        /// Property: Locale switching should be consistent
        #[test]
        fn prop_locale_switching_consistency(
            en_translations in arb_translations(),
            zh_translations in arb_translations(),
        ) {
            let config = LocalizationConfig {
                default_locale: "en-US".to_string(),
                fallback_locale: "en-US".to_string(),
                supported_locales: vec!["en-US".to_string(), "zh-CN".to_string()],
            };

            let mut manager = LocalizationManager::new(config);
            manager.load_translations("en-US", en_translations.clone());
            manager.load_translations("zh-CN", zh_translations.clone());

            // Get a key that exists in both locales
            let common_keys: Vec<_> = en_translations.keys()
                .filter(|k| zh_translations.contains_key(*k))
                .collect();

            if let Some(key) = common_keys.first() {
                // In en-US locale
                manager.set_locale("en-US").unwrap();
                let en_result = manager.get_text(key);
                prop_assert_eq!(&en_result, en_translations.get(*key).unwrap());

                // Switch to zh-CN
                manager.set_locale("zh-CN").unwrap();
                let zh_result = manager.get_text(key);
                prop_assert_eq!(&zh_result, zh_translations.get(*key).unwrap());

                // Switch back to en-US
                manager.set_locale("en-US").unwrap();
                let en_result_again = manager.get_text(key);
                prop_assert_eq!(en_result, en_result_again,
                    "Switching back should give same result");
            }
        }
    }
}
