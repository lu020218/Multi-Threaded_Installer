(async function() {
  let tauriInvoke = null;
  try {
    console.log('Loading custom UI content...');

    let attempts = 0;
    while (!window.__TAURI__ && attempts < 50) {
      await new Promise(r => setTimeout(r, 100));
      attempts++;
    }

    if (!window.__TAURI__) {
      console.error('Tauri API not available');
      return;
    }

    const resolveInvoke = () => (
      (window.__TAURI__ && window.__TAURI__.core && window.__TAURI__.core.invoke) ||
      (window.__TAURI__ && window.__TAURI__.invoke) ||
      (window.__TAURI__ && window.__TAURI__.tauri && window.__TAURI__.tauri.invoke)
    );
    const resolveListen = () => (
      (window.__TAURI__ && window.__TAURI__.event && window.__TAURI__.event.listen)
    );

    tauriInvoke = resolveInvoke();
    const tauriListen = resolveListen();
    if (!tauriInvoke || !tauriListen) {
      console.error('Tauri API incomplete: invoke/listen not available');
      return;
    }

    window.tauriInvoke = tauriInvoke;
    window.tauriListen = tauriListen;

    const content = await tauriInvoke('get_custom_ui_content');
    if (!(content && content.html)) {
      console.log('No custom UI content available, using default UI');
      return;
    }

    console.log('Injecting custom HTML...');

    const parser = new DOMParser();
    const doc = parser.parseFromString(content.html, 'text/html');

    const existingStyles = document.querySelectorAll('style:not([data-tauri])');
    existingStyles.forEach(s => s.remove());

    const existingLinks = document.querySelectorAll('link[rel="stylesheet"]');
    existingLinks.forEach(l => l.remove());

    if (content.css) {
      const style = document.createElement('style');
      style.id = 'custom-ui-styles';
      style.textContent = content.css;
      document.head.appendChild(style);
      console.log('Custom CSS injected');
    }

    document.body.innerHTML = doc.body.innerHTML;
    for (const attr of doc.body.attributes) {
      document.body.setAttribute(attr.name, attr.value);
    }

    if (content.js) {
      await new Promise(r => setTimeout(r, 100));
      try {
        (0, eval)(content.js);
        console.log('Custom JS executed via indirect eval');
        await new Promise(r => setTimeout(r, 100));
        if (typeof window.initializeApp === 'function') {
          console.log('Calling window.initializeApp...');
          await window.initializeApp();
          console.log('initializeApp completed');
        } else {
          console.warn('window.initializeApp not found after script execution');
        }
      } catch (e) {
        console.error('Error executing custom JS:', e);
      }
    }

    console.log('Custom UI injection complete');
  } catch (error) {
    console.error('Failed to load custom UI:', error);
  } finally {
    try {
      if (typeof tauriInvoke === 'function') {
        await tauriInvoke('notify_ui_ready');
      } else if (window.__TAURI__ && window.__TAURI__.core && window.__TAURI__.core.invoke) {
        await window.__TAURI__.core.invoke('notify_ui_ready');
      }
    } catch (e) {
      console.warn('notify_ui_ready failed:', e);
    }
  }
})();
