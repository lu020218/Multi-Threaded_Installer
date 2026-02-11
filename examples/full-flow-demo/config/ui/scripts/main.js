// Installer UI Template Script
// This script is designed to work both when loaded normally and when injected dynamically

// Get Tauri API - check multiple sources
var invoke = window.tauriInvoke || (window.__TAURI__ && window.__TAURI__.core.invoke);
var listen = window.tauriListen || (window.__TAURI__ && window.__TAURI__.event.listen);

// State
var currentPage = 'welcome';
var metadata = null;
var translations = {};
var currentLocale = 'en-US';
var packagePath = null;
var isBrowsing = false; // Flag to prevent multiple browse dialogs
var lastPhase = null; // Track the last phase for progress calculation
var phaseProgress = {}; // Track progress per phase

// DOM Elements - will be populated when needed
var pages = {};
var hasLicense = false;
var licenseAccepted = false;

// Make initializeApp globally accessible for dynamic injection
window.initializeApp = initializeApp;

// Initialize on DOMContentLoaded (for normal loading)
if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', function() {
        initializeApp();
    });
}

async function initializeApp() {
    if (window.__installer_ui_initialized) {
        console.log('initializeApp already called, skipping');
        return;
    }
    window.__installer_ui_initialized = true;
    try {
        console.log('initializeApp called');
        
        var isPreview = false;
        try {
            var params = new URLSearchParams(window.location.search || '');
            isPreview = params.get('preview') === '1';
        } catch (e) {
            isPreview = false;
        }

        // Re-check Tauri API availability
        if (!invoke) {
            invoke = window.tauriInvoke || (window.__TAURI__ && window.__TAURI__.core && window.__TAURI__.core.invoke) || (window.__TAURI__ && window.__TAURI__.invoke);
        }
        if (!listen) {
            listen = window.tauriListen || (window.__TAURI__ && window.__TAURI__.event && window.__TAURI__.event.listen);
        }
        
    if (!invoke && !isPreview) {
        console.error('Tauri invoke not available');
        return;
    }
    if (isPreview) {
        invoke = invoke || function() {
            return Promise.reject(new Error('Tauri invoke unavailable in preview mode'));
        };
        listen = listen || function() {
            return Promise.reject(new Error('Tauri listen unavailable in preview mode'));
        };
    }
        
        // Initialize pages object (DOM elements)
        pages.welcome = document.getElementById('welcome-page');
        pages.license = document.getElementById('license-page');
        pages.directory = document.getElementById('directory-page');
        pages.progress = document.getElementById('progress-page');
        pages.complete = document.getElementById('complete-page');
        pages.error = document.getElementById('error-page');
        console.log('Pages initialized:', Object.keys(pages).filter(function(k) { return pages[k]; }));
        
        if (isPreview) {
            packagePath = './package.mti';
            metadata = {
                app_name: 'Demo Installer',
                version: '1.0.0',
                default_install_dir: 'C:\\Program Files\\Demo',
                vendor: 'Demo Vendor',
                license_text: 'License preview (design mode only).',
                require_admin: false,
                desktop_icons: true,
                auto_startup: false,
            };
            currentLocale = 'zh-CN';
            translations = {};
            applyTranslations();
            var licenseLinkContainer = document.getElementById('license-link-container');
            var licenseText = document.getElementById('license-text');
            if (licenseLinkContainer) {
                licenseLinkContainer.style.display = 'flex';
            }
            if (licenseText) {
                licenseText.textContent = metadata.license_text;
            }
        } else {
            // Get package path - check for embedded package first
            try {
                packagePath = await invoke('get_embedded_package_path');
            } catch (e) {
                console.log('No embedded package, using default path');
            }
            if (!packagePath) {
                packagePath = './package.mti';
            }
            console.log('Using package path:', packagePath);
            
            // Detect system locale
            try {
                currentLocale = await invoke('get_system_locale');
            } catch (e) {
                console.log('Failed to get locale:', e);
            }
            
            // Load translations
            await loadTranslations(currentLocale);
            
            // Load metadata
            await loadMetadata();
        }
        
        // Setup event listeners
        setupEventListeners();
        if (!isPreview) {
            await setupTauriListeners();
        }
        setupWindowControls();
        
        // Apply translations
        applyTranslations();
        
        console.log('Initialization complete');
    } catch (error) {
        console.error('Failed to initialize:', error);
    }
}

function setupWindowControls() {
    var minimizeBtn = document.getElementById('titlebar-minimize');
    var closeBtn = document.getElementById('titlebar-close');
    var titlebar = document.getElementById('titlebar');
    
    if (minimizeBtn) {
        minimizeBtn.onclick = async function(e) {
            e.preventDefault();
            e.stopPropagation();
            try {
                await invoke('minimize_window');
            } catch (error) {
                console.error('Failed to minimize window:', error);
            }
        };
        console.log('Minimize button handler attached');
    }
    
    if (closeBtn) {
        closeBtn.onclick = async function(e) {
            e.preventDefault();
            e.stopPropagation();
            try {
                await invoke('close_window');
            } catch (error) {
                console.error('Failed to close window:', error);
            }
        };
        console.log('Close button handler attached');
    }
    
    // Setup window dragging for titlebar
    if (titlebar) {
        setupWindowDrag(titlebar);
        console.log('Window drag handler attached');
    }
}

// Setup window dragging functionality
function setupWindowDrag(element) {
    element.addEventListener('mousedown', async function(e) {
        // Don't drag if clicking on buttons
        if (e.target.closest('button') || e.target.closest('.titlebar-buttons')) {
            return;
        }
        
        // Prevent text selection while dragging
        e.preventDefault();
        
        try {
            // Use our custom Rust command for window dragging
            await invoke('start_dragging');
        } catch (err) {
            console.log('start_dragging error:', err);
        }
    });
}

async function loadMetadata() {
    try {
        metadata = await invoke('get_metadata', { packagePath: packagePath });
        
        var appName = document.getElementById('app-name');
        var appVersion = document.getElementById('app-version');
        var installDir = document.getElementById('install-dir');
        var licenseText = document.getElementById('license-text');
        var licenseLinkContainer = document.getElementById('license-link-container');
        var createShortcuts = document.getElementById('create-shortcuts');
        var autoStartup = document.getElementById('auto-startup');
        var titlebarAppName = document.getElementById('titlebar-app-name');
        
        if (appName) appName.textContent = metadata.app_name;
        if (appVersion) appVersion.textContent = 'Version ' + metadata.version;
        if (installDir) installDir.value = metadata.default_install_dir;
        if (titlebarAppName) titlebarAppName.textContent = metadata.app_name + ' Setup';
        
        // Handle license
        if (metadata.license_text && licenseText) {
            hasLicense = true;
            licenseText.textContent = metadata.license_text;
            if (licenseLinkContainer) {
                licenseLinkContainer.style.display = 'flex';
            }
        }
        
        if (createShortcuts) createShortcuts.checked = metadata.desktop_icons;
        if (autoStartup) autoStartup.checked = metadata.auto_startup;

        await applyWindowConfig(metadata.window);
        
        console.log('Metadata loaded:', metadata.app_name, 'hasLicense:', hasLicense);
    } catch (error) {
        console.error('Failed to load metadata:', error);
    }
}

async function loadTranslations(locale) {
    try {
        var response = await invoke('get_translations', { locale: locale });
        translations = response.translations;
        currentLocale = response.locale;
    } catch (error) {
        console.error('Failed to load translations:', error);
        translations = {};
    }
}

function applyTranslations() {
    var elements = document.querySelectorAll('[data-i18n]');
    elements.forEach(function(element) {
        var key = element.getAttribute('data-i18n');
        var text = translations[key] || element.textContent;
        
        if (metadata) {
            text = text.replace('{appName}', metadata.app_name);
        }
        
        element.textContent = text;
    });
}

function t(key, vars) {
    vars = vars || {};
    var text = translations[key] || key;
    for (var name in vars) {
        text = text.replace('{' + name + '}', vars[name]);
    }
    return text;
}

async function applyWindowConfig(windowConfig) {
    if (!windowConfig || !invoke) {
        return;
    }
    try {
        await invoke('apply_window_config', windowConfig);
    } catch (error) {
        console.warn('Failed to apply window config:', error);
    }
}

function setupEventListeners() {
    // Welcome page buttons
    var btnCancel = document.getElementById('btn-cancel');
    var btnNext = document.getElementById('btn-next');
    var licenseLink = document.getElementById('license-link');
    var btnCustomOptions = document.getElementById('btn-custom-options');
    
    if (btnCancel) {
        btnCancel.onclick = function() {
            invoke('close_window').catch(function() { window.close(); });
        };
    }
    
    if (btnNext) {
        btnNext.onclick = function() {
            var acceptLicenseCheckbox = document.getElementById('accept-license');
            if (hasLicense) {
                var accepted = licenseAccepted || (acceptLicenseCheckbox && acceptLicenseCheckbox.checked);
                if (!accepted) {
                    showPage('license');
                    return;
                }
                licenseAccepted = true;
            }
            startInstall();
        };
    }

    if (btnCustomOptions) {
        btnCustomOptions.onclick = function() {
            // if (hasLicense && !licenseAccepted) {
            //     showPage('license');
            //     return;
            // }
            showPage('directory');
        };
    }
    
    // License link in welcome page
    if (licenseLink) {
        licenseLink.onclick = function(e) {
            e.preventDefault();
            showPage('license');
        };
    }
    
    // License page buttons
    var btnLicenseBack = document.getElementById('btn-license-back');
    var btnLicenseAccept = document.getElementById('btn-license-accept');
    var acceptLicenseCheckbox = document.getElementById('accept-license');
    
    if (btnLicenseBack) {
        btnLicenseBack.onclick = function() { showPage('welcome'); };
    }
    
    if (btnLicenseAccept) {
        btnLicenseAccept.onclick = function() {
            if (acceptLicenseCheckbox && !acceptLicenseCheckbox.checked) {
                alert(t('error.license_required') || 'Please accept the license agreement');
                return;
            }
            licenseAccepted = true;
            showPage('directory');
        };
    }
    
    // Directory page buttons
    var btnBack = document.getElementById('btn-back');
    var btnBrowse = document.getElementById('btn-browse');
    var btnInstall = document.getElementById('btn-install');
    
    if (btnBack) {
        btnBack.onclick = function() { 
            // Go back to license page if has license, otherwise welcome
            if (hasLicense) {
                showPage('license');
            } else {
                showPage('welcome');
            }
        };
    }
    
    if (btnBrowse) {
        btnBrowse.onclick = browseDirectory;
    }
    
    if (btnInstall) {
        btnInstall.onclick = startInstall;
    }
    
    // Progress page
    var btnCancelInstall = document.getElementById('btn-cancel-install');
    if (btnCancelInstall) {
        btnCancelInstall.onclick = cancelInstall;
    }
    
    // Complete page
    var btnFinish = document.getElementById('btn-finish');
    if (btnFinish) {
        btnFinish.onclick = function() {
            invoke('close_window').catch(function() { window.close(); });
        };
    }
    
    // Error page
    var btnRetry = document.getElementById('btn-retry');
    var btnClose = document.getElementById('btn-close');
    
    if (btnRetry) {
        btnRetry.onclick = function() { showPage('directory'); };
    }
    
    if (btnClose) {
        btnClose.onclick = function() {
            invoke('close_window').catch(function() { window.close(); });
        };
    }
    
    // Language selector
    var languageSelect = document.getElementById('language-select');
    if (languageSelect) {
        languageSelect.onchange = async function(e) {
            currentLocale = e.target.value;
            await loadTranslations(currentLocale);
            applyTranslations();
        };
    }
    
    console.log('Event listeners setup complete');
}

async function setupTauriListeners() {
    // Always re-fetch listen function to ensure we have the latest reference
    // This is important when UI is dynamically injected
    var listenFn = window.tauriListen || (window.__TAURI__ && window.__TAURI__.event && window.__TAURI__.event.listen);
    
    if (!listenFn) {
        console.warn('Tauri listen not available, waiting...');
        // Wait a bit and retry
        await new Promise(function(resolve) { setTimeout(resolve, 200); });
        listenFn = window.tauriListen || (window.__TAURI__ && window.__TAURI__.event && window.__TAURI__.event.listen);
    }
    
    if (listenFn) {
        console.log('Setting up Tauri event listeners, listenFn:', typeof listenFn);
        
        try {
            // listen returns a Promise that resolves to an unlisten function
            var unlisten1 = await listenFn('install_progress', function(event) { 
                console.log('Progress event received:', JSON.stringify(event));
                if (event && event.payload) {
                    updateProgress(event.payload);
                } else {
                    console.warn('Progress event missing payload:', event);
                }
            });
            console.log('install_progress listener registered, unlisten:', typeof unlisten1);
            
            var unlisten2 = await listenFn('install_complete', function(event) { 
                console.log('Complete event received:', JSON.stringify(event));
                if (event && event.payload) {
                    showComplete(event.payload);
                }
            });
            console.log('install_complete listener registered');
            
            var unlisten3 = await listenFn('install_error', function(event) { 
                console.log('Error event received:', JSON.stringify(event));
                if (event && event.payload) {
                    showError(event.payload);
                } else {
                    showError(event);
                }
            });
            console.log('install_error listener registered');
            
            console.log('All Tauri event listeners registered successfully');
        } catch (err) {
            console.error('Failed to register event listeners:', err);
        }
    } else {
        console.error('Tauri listen function not available after retry');
        console.log('window.tauriListen:', typeof window.tauriListen);
        console.log('window.__TAURI__:', typeof window.__TAURI__);
        if (window.__TAURI__) {
            console.log('window.__TAURI__.event:', typeof window.__TAURI__.event);
        }
    }
}

function showPage(pageName) {
    Object.keys(pages).forEach(function(key) {
        if (pages[key]) {
            pages[key].classList.remove('active');
        }
    });
    if (pages[pageName]) {
        pages[pageName].classList.add('active');
    }
    currentPage = pageName;
}

async function browseDirectory() {
    // Prevent multiple dialogs from opening
    if (isBrowsing) {
        console.log('Browse already in progress, ignoring click');
        return;
    }
    
    isBrowsing = true;
    try {
        var installDir = document.getElementById('install-dir');
        var result = await invoke('browse_directory', {
            defaultPath: installDir ? installDir.value : ''
        });
        if (result && installDir) {
            installDir.value = result;
        }
    } catch (error) {
        console.error('Failed to browse:', error);
    } finally {
        isBrowsing = false;
    }
}

async function startInstall() {
    console.log('startInstall called');
    if (!packagePath) {
        showError('No package available for installation');
        return;
    }

    var components = collectComponentSelections();
    var installDir = document.getElementById('install-dir');
    var createShortcuts = document.getElementById('create-shortcuts');
    var autoStartup = document.getElementById('auto-startup');

    var request = {
        package_path: packagePath,
        install_dir: installDir ? installDir.value : '',
        create_shortcuts: createShortcuts ? createShortcuts.checked : true,
        auto_startup: autoStartup ? autoStartup.checked : false,
    };
    if (components) {
        request.components = components;
    }

    var validation;
    try {
        validation = await invoke('validate_install_request', { request: request });
    } catch (error) {
        showError('Failed to validate installation: ' + error);
        return;
    }

    if (!validation || validation.ok !== true) {
        var errorItem = validation && validation.errors && validation.errors.length > 0
            ? validation.errors[0]
            : null;
        var message = errorItem
            ? (errorItem.message + (errorItem.detail ? ' (' + errorItem.detail + ')' : ''))
            : 'Installation prerequisites failed';
        showError(message);
        return;
    }

    if (validation.warnings && validation.warnings.length > 0) {
        console.warn('Validation warnings:', validation.warnings);
    }

    showPage('progress');
    
    // Reset progress tracking
    lastPhase = null;
    phaseProgress = {};
    
    var progressFill = document.getElementById('progress-fill');
    var progressText = document.getElementById('progress-text');
    
    if (progressFill) progressFill.style.width = '0%';
    if (progressText) progressText.textContent = '0%';
    
    try {
        console.log('Calling start_install with request:', JSON.stringify(request));

        await invoke('start_install', { request: request });
        
        console.log('start_install completed');
    } catch (error) {
        console.error('start_install error:', error);
        showError(error);
    }
}

function collectComponentSelections() {
    var chrome = document.getElementById('accept-chrome-plugin');
    var ppt = document.getElementById('accept-ppt-plugin');
    var hasAny = false;
    var components = {};

    if (chrome) {
        components['chrome-plugin'] = !!chrome.checked;
        hasAny = true;
    }
    if (ppt) {
        components['ppt-plugin'] = !!ppt.checked;
        hasAny = true;
    }

    return hasAny ? components : null;
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
    console.log('updateProgress called with:', JSON.stringify(progress));
    
    // Use overall_percentage from backend if available (already calculated with phase weights)
    var overallPercentage = progress.overall_percentage;
    
    // Fallback calculation if overall_percentage not provided
    if (typeof overallPercentage !== 'number') {
        // Phase weights for overall progress calculation
        // Decompressing: 40%, Writing: 55%, Completing: 5%
        var phaseWeights = {
            'Decompressing': { start: 0, weight: 40 },
            'Writing': { start: 40, weight: 55 },
            'Completing': { start: 95, weight: 5 }
        };
        
        var phase = progress.phase || 'Unknown';
        var phasePercentage = 0;
        
        if (progress.total > 0) {
            phasePercentage = (progress.current / progress.total) * 100;
        }
        
        var phaseInfo = phaseWeights[phase];
        if (phaseInfo) {
            overallPercentage = phaseInfo.start + (phasePercentage * phaseInfo.weight / 100);
        } else {
            overallPercentage = phasePercentage;
        }
    }
    
    var progressFill = document.getElementById('progress-fill');
    var progressText = document.getElementById('progress-text');
    var progressPhase = document.getElementById('progress-phase');
    var currentFile = document.getElementById('current-file');
    
    // Update progress bar (backend already ensures monotonic increase)
    if (progressFill) {
        progressFill.style.width = overallPercentage + '%';
    }
    if (progressText) {
        progressText.textContent = Math.round(overallPercentage) + '%';
    }
    
    // Update phase display
    if (progressPhase && progress.phase) {
        var phaseText = progress.phase;
        // Translate phase names
        var phaseTranslations = {
            'Decompressing': '解压中',
            'Writing': '写入文件',
            'Completing': '完成中'
        };
        if (currentLocale.startsWith('zh') && phaseTranslations[progress.phase]) {
            phaseText = phaseTranslations[progress.phase];
        }
        progressPhase.textContent = phaseText;
    }
    
    // Update current file display
    if (progress.current_file && currentFile) {
        // Show only the filename, not the full path
        var fileName = progress.current_file;
        var lastSlash = Math.max(fileName.lastIndexOf('/'), fileName.lastIndexOf('\\'));
        if (lastSlash >= 0) {
            fileName = fileName.substring(lastSlash + 1);
        }
        currentFile.textContent = fileName;
    }
}

function showComplete(stats) {
    var statsFiles = document.getElementById('stats-files');
    var statsSize = document.getElementById('stats-size');
    var statsTime = document.getElementById('stats-time');
    
    if (statsFiles) statsFiles.textContent = stats.files || '0';
    if (statsSize) statsSize.textContent = formatSize(stats.size);
    if (statsTime) statsTime.textContent = formatTime(stats.time_ms);
    
    showPage('complete');
}

function showError(error) {
    var message = typeof error === 'object' ? error.message : error;
    var errorMessage = document.getElementById('error-message');
    if (errorMessage) errorMessage.textContent = message;
    showPage('error');
}

function formatSize(bytes) {
    if (!bytes) return '0 bytes';
    if (bytes >= 1000000000) return (bytes / 1000000000).toFixed(2) + ' GB';
    if (bytes >= 1000000) return (bytes / 1000000).toFixed(2) + ' MB';
    if (bytes >= 1000) return (bytes / 1000).toFixed(2) + ' KB';
    return bytes + ' bytes';
}

function formatTime(ms) {
    if (!ms) return '0s';
    if (ms >= 60000) {
        var minutes = Math.floor(ms / 60000);
        var seconds = Math.floor((ms % 60000) / 1000);
        return minutes + 'm ' + seconds + 's';
    }
    return (ms / 1000).toFixed(1) + 's';
}
