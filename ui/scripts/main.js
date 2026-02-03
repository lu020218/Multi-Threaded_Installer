// Installer UI Main Script
// Requirements: 4.5, 4.6, 4.7, 4.8 - UI pages implementation

// Tauri 2.0 API - wait for Tauri to be ready
let invoke;
let listen;
let appWindow;

// State
let currentPage = 'welcome';
let metadata = null;
let installDir = '';
let translations = {};
let currentLocale = 'en-US';
let packagePath = null;
let isBrowsingDirectory = false;

// DOM Elements
const pages = {
    welcome: document.getElementById('welcome-page'),
    directory: document.getElementById('directory-page'),
    progress: document.getElementById('progress-page'),
    complete: document.getElementById('complete-page'),
    error: document.getElementById('error-page'),
};

// Initialize
document.addEventListener('DOMContentLoaded', async () => {
    // Wait for Tauri API to be available
    await waitForTauri();
    // Setup window controls first
    setupWindowControls();
    await initializeApp();
});

// Wait for Tauri global API to be injected
async function waitForTauri() {
    return new Promise((resolve, reject) => {
        let attempts = 0;
        const maxAttempts = 50; // 5 seconds max
        
        const checkTauri = () => {
            if (window.__TAURI__) {
                // Tauri 2.0 API structure
                invoke = window.__TAURI__.core.invoke;
                listen = window.__TAURI__.event.listen;
                appWindow = window.__TAURI__.window.getCurrentWindow();
                console.log('Tauri API initialized');
                resolve();
            } else if (attempts < maxAttempts) {
                attempts++;
                setTimeout(checkTauri, 100);
            } else {
                console.error('Tauri API not available after timeout');
                reject(new Error('Tauri API not available'));
            }
        };
        
        checkTauri();
    });
}

// Setup custom window controls
function setupWindowControls() {
    const minimizeBtn = document.getElementById('titlebar-minimize');
    const closeBtn = document.getElementById('titlebar-close');
    
    if (minimizeBtn) {
        minimizeBtn.addEventListener('click', async (e) => {
            e.preventDefault();
            e.stopPropagation();
            try {
                // Use Rust command for reliable window control
                await invoke('minimize_window');
            } catch (error) {
                console.error('Failed to minimize window:', error);
            }
        });
    }
    
    if (closeBtn) {
        closeBtn.addEventListener('click', async (e) => {
            e.preventDefault();
            e.stopPropagation();
            try {
                // Use Rust command for reliable window control
                await invoke('close_window');
            } catch (error) {
                console.error('Failed to close window:', error);
            }
        });
    }
    
    console.log('Window controls setup complete');
}

async function initializeApp() {
    try {
        // Detect system locale
        currentLocale = await invoke('get_system_locale');
        document.getElementById('language-select').value = currentLocale.startsWith('zh') ? 'zh-CN' : 'en-US';
        
        // Load translations
        await loadTranslations(currentLocale);
        
        // Check for embedded package first
        packagePath = await getPackagePath();
        
        // Load metadata
        await loadMetadata();
        
        // Setup event listeners
        setupEventListeners();
        setupTauriListeners();
        
        // Apply translations
        applyTranslations();
    } catch (error) {
        console.error('Failed to initialize:', error);
        showError('Failed to initialize installer: ' + error);
    }
}

async function getPackagePath() {
    try {
        // First check for embedded package
        const embeddedPath = await invoke('get_embedded_package_path');
        if (embeddedPath) {
            console.log('Using embedded package:', embeddedPath);
            return embeddedPath;
        }
    } catch (error) {
        console.log('No embedded package found');
    }
    
    // Fall back to external package file
    return './package.mti';
}

async function loadMetadata() {
    try {
        if (!packagePath) {
            throw new Error('No package path available');
        }
        
        metadata = await invoke('get_metadata', { packagePath });
        
        // Update UI with metadata
        document.getElementById('app-name').textContent = metadata.app_name;
        document.getElementById('app-version').textContent = `Version ${metadata.version}`;
        document.getElementById('install-dir').value = metadata.default_install_dir;
        installDir = metadata.default_install_dir;
        
        // Update titlebar with app name
        const titlebarAppName = document.getElementById('titlebar-app-name');
        if (titlebarAppName) {
            titlebarAppName.textContent = `${metadata.app_name} Setup`;
        }
        
        // Show license if available
        if (metadata.license_text) {
            document.getElementById('license-section').style.display = 'block';
            document.getElementById('license-text').textContent = metadata.license_text;
        }
        
        // Set default options based on metadata
        document.getElementById('create-shortcuts').checked = metadata.desktop_icons;
        document.getElementById('auto-startup').checked = metadata.auto_startup;
        
        // Check prerequisites
        await checkPrerequisites();
    } catch (error) {
        console.error('Failed to load metadata:', error);
        showError('Failed to load package: ' + error);
    }
}

async function loadTranslations(locale) {
    try {
        const response = await invoke('get_translations', { locale });
        translations = response.translations;
        currentLocale = response.locale;
    } catch (error) {
        console.error('Failed to load translations:', error);
        // Use default translations
        translations = getDefaultTranslations(locale);
    }
}

function getDefaultTranslations(locale) {
    if (locale.startsWith('zh')) {
        return {
            'welcome.description': '这将在您的计算机上安装 {appName}。',
            'install.directory': '安装目录',
            'install.progress': '正在安装...',
            'install.complete': '安装完成',
            'install.success': '应用程序已成功安装。',
            'button.next': '下一步',
            'button.back': '上一步',
            'button.install': '安装',
            'button.cancel': '取消',
            'button.finish': '完成',
            'button.browse': '浏览',
        };
    }
    return {
        'welcome.description': 'This will install {appName} on your computer.',
        'install.directory': 'Installation Directory',
        'install.progress': 'Installing...',
        'install.complete': 'Installation Complete',
        'install.success': 'The application has been installed successfully.',
        'button.next': 'Next',
        'button.back': 'Back',
        'button.install': 'Install',
        'button.cancel': 'Cancel',
        'button.finish': 'Finish',
        'button.browse': 'Browse',
    };
}

function applyTranslations() {
    document.querySelectorAll('[data-i18n]').forEach(element => {
        const key = element.getAttribute('data-i18n');
        let text = translations[key] || element.textContent;
        
        // Replace variables
        if (metadata) {
            text = text.replace('{appName}', metadata.app_name);
        }
        
        element.textContent = text;
    });
}

function t(key, vars = {}) {
    let text = translations[key] || key;
    for (const [name, value] of Object.entries(vars)) {
        text = text.replace(`{${name}}`, value);
    }
    return text;
}

async function checkPrerequisites() {
    try {
        if (!packagePath) {
            console.warn('No package path available for prerequisites check');
            return;
        }
        const result = await invoke('check_prerequisites', {
            packagePath: packagePath,
            installDir: document.getElementById('install-dir').value,
        });
        
        // Update space info
        document.getElementById('required-space').textContent = `${result.required_space_mb} MB`;
        document.getElementById('available-space').textContent = `${result.available_space_mb} MB`;
        
        // Check for issues
        if (!result.disk_space_ok) {
            showWarning(t('error.disk_space'));
        }
        
        if (result.process_running) {
            showWarning(t('error.process_running'));
        }
        
        if (result.admin_required && !result.is_admin) {
            showWarning('Administrator privileges required');
        }
    } catch (error) {
        console.error('Failed to check prerequisites:', error);
    }
}

function showWarning(message) {
    // Could show a toast or inline warning
    console.warn(message);
}

function setupEventListeners() {
    // Welcome page
    document.getElementById('btn-cancel').addEventListener('click', () => {
        window.close();
    });
    
    document.getElementById('btn-next').addEventListener('click', () => {
        // Check license acceptance if shown
        const licenseSection = document.getElementById('license-section');
        if (licenseSection.style.display !== 'none') {
            const accepted = document.getElementById('accept-license').checked;
            if (!accepted) {
                alert(t('error.license_required') || 'Please accept the license agreement to continue.');
                return;
            }
        }
        showPage('directory');
    });
    
    // Directory page
    document.getElementById('btn-back').addEventListener('click', () => {
        showPage('welcome');
    });
    
    document.getElementById('btn-browse').addEventListener('click', browseDirectory);
    
    document.getElementById('btn-install').addEventListener('click', startInstall);
    
    // Progress page
    document.getElementById('btn-cancel-install').addEventListener('click', cancelInstall);
    
    // Complete page
    document.getElementById('btn-finish').addEventListener('click', () => {
        // Could launch app if checkbox is checked
        window.close();
    });
    
    // Error page
    document.getElementById('btn-retry').addEventListener('click', () => {
        showPage('directory');
    });
    
    document.getElementById('btn-close').addEventListener('click', () => {
        window.close();
    });
    
    // Language selector
    document.getElementById('language-select').addEventListener('change', async (e) => {
        currentLocale = e.target.value;
        await loadTranslations(currentLocale);
        applyTranslations();
        await invoke('set_locale', { locale: currentLocale });
    });
}

function setupTauriListeners() {
    // Progress events
    listen('install_progress', (event) => {
        const progress = event.payload;
        updateProgress(progress);
    });
    
    // Completion event
    listen('install_complete', (event) => {
        const stats = event.payload;
        showComplete(stats);
    });
    
    // Error event
    listen('install_error', (event) => {
        const error = event.payload;
        showError(error);
    });
    
    // Cancellation event
    listen('install_cancelled', () => {
        showPage('welcome');
    });
}

function showPage(pageName) {
    Object.values(pages).forEach(page => {
        if (page) page.classList.remove('active');
    });
    if (pages[pageName]) {
        pages[pageName].classList.add('active');
    }
    currentPage = pageName;
}

async function browseDirectory() {
    if (isBrowsingDirectory) {
        return;
    }
    try {
        isBrowsingDirectory = true;
        const browseBtn = document.getElementById('btn-browse');
        if (browseBtn) {
            browseBtn.disabled = true;
        }

        const defaultPath = document.getElementById('install-dir').value;
        const result = await invoke('browse_directory', { defaultPath });
        
        if (result) {
            document.getElementById('install-dir').value = result;
            installDir = result;
            await checkPrerequisites();
        }
    } catch (error) {
        console.error('Failed to browse directory:', error);
    } finally {
        isBrowsingDirectory = false;
        const browseBtn = document.getElementById('btn-browse');
        if (browseBtn) {
            browseBtn.disabled = false;
        }
    }
}

async function startInstall() {
    showPage('progress');
    
    // Reset progress
    document.getElementById('progress-fill').style.width = '0%';
    document.getElementById('progress-text').textContent = '0%';
    document.getElementById('progress-phase').textContent = '';
    document.getElementById('current-file').textContent = '';
    document.getElementById('progress-speed').textContent = '';
    
    if (!packagePath) {
        showError('No package available for installation');
        return;
    }
    
    const request = {
        package_path: packagePath,
        install_dir: document.getElementById('install-dir').value,
        create_shortcuts: document.getElementById('create-shortcuts').checked,
        auto_startup: document.getElementById('auto-startup').checked,
    };
    
    try {
        await invoke('start_install', { request });
    } catch (error) {
        showError(error);
    }
}

async function cancelInstall() {
    try {
        await invoke('cancel_install');
        showPage('welcome');
    } catch (error) {
        console.error('Failed to cancel:', error);
    }
}

function updateProgress(progress) {
    const fallbackPercentage = (progress.total > 0)
        ? ((progress.current / progress.total) * 100)
        : 0;
    const percentage = (progress.overall_percentage ?? progress.percentage ?? fallbackPercentage);
    
    document.getElementById('progress-fill').style.width = `${percentage}%`;
    document.getElementById('progress-text').textContent = `${Math.round(percentage)}%`;
    
    // Update phase
    const phaseText = progress.phase || '';
    document.getElementById('progress-phase').textContent = phaseText;
    
    // Update current file
    if (progress.current_file) {
        document.getElementById('current-file').textContent = progress.current_file;
    }
    
    // Update speed
    if (progress.speed_display) {
        document.getElementById('progress-speed').textContent = progress.speed_display;
    }
}

function showComplete(stats) {
    document.getElementById('stats-files').textContent = stats.files || '0';
    document.getElementById('stats-size').textContent = stats.size_display || formatSize(stats.size);
    document.getElementById('stats-time').textContent = stats.time_display || formatTime(stats.time_ms);
    
    showPage('complete');
}

function showError(error) {
    let message = error;
    let code = null;
    let suggestion = null;
    
    if (typeof error === 'object') {
        message = error.message || 'An unknown error occurred';
        code = error.code;
        suggestion = error.suggestion;
    }
    
    document.getElementById('error-message').textContent = message;
    
    if (code) {
        document.getElementById('error-details').style.display = 'block';
        document.getElementById('error-code').textContent = code;
    } else {
        document.getElementById('error-details').style.display = 'none';
    }
    
    if (suggestion) {
        document.getElementById('error-suggestion').style.display = 'block';
        document.getElementById('error-suggestion').textContent = suggestion;
    } else {
        document.getElementById('error-suggestion').style.display = 'none';
    }
    
    showPage('error');
}

function formatSize(bytes) {
    if (!bytes) return '0 bytes';
    if (bytes >= 1000000000) return `${(bytes / 1000000000).toFixed(2)} GB`;
    if (bytes >= 1000000) return `${(bytes / 1000000).toFixed(2)} MB`;
    if (bytes >= 1000) return `${(bytes / 1000).toFixed(2)} KB`;
    return `${bytes} bytes`;
}

function formatTime(ms) {
    if (!ms) return '0s';
    if (ms >= 60000) {
        const minutes = Math.floor(ms / 60000);
        const seconds = Math.floor((ms % 60000) / 1000);
        return `${minutes}m ${seconds}s`;
    }
    return `${(ms / 1000).toFixed(1)}s`;
}
