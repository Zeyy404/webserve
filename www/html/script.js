// UI Elements
const select = document.getElementById('filePath');
const resultMessage = document.getElementById('resultMessage');
const showLoginBtn = document.getElementById('show-login-btn');
const logoutForm = document.getElementById('logout-form');
const userDisplay = document.getElementById('user-display');
const loginPanel = document.getElementById('login-panel');
const filePanel = document.getElementById('file-panel');
const panelTitle = document.getElementById('panel-title');
const cancelLoginBtn = document.getElementById('cancel-login');
const uploadButton = document.getElementById('upload-link');

// Toggle the login form 
showLoginBtn.addEventListener('click', () => {
    loginPanel.classList.remove('hidden');
    filePanel.classList.add('hidden');
});

cancelLoginBtn.addEventListener('click', () => {
    loginPanel.classList.add('hidden');
    filePanel.classList.remove('hidden');
});

// Load files depending on session state (Clean async/await structure)
async function loadFileOptions(fetchUrl) {
    try {
        const res = await fetch(fetchUrl);
        const html = await res.text();
        const doc = new DOMParser().parseFromString(html, 'text/html');
        
        select.innerHTML = '';
        
        doc.querySelectorAll('a').forEach(a => {
            const href = decodeURIComponent(a.getAttribute('href'));
            // Strip leading paths to get just the filename
            const name = href.replace(/^(\/?uploads\/?|\/?my-uploads\/?)/, '');
            
            if (name && name !== '.' && name !== '..') {
                const opt = document.createElement('option');
                opt.value = name;
                opt.textContent = name;
                select.appendChild(opt);
            }
        });
    } catch (error) {
        console.error('Error loading files:', error);
    }
}

// Delete action (Async/await with template literals)
document.getElementById('deleteButton').addEventListener('click', async () => {
    const filePath = select.value;
    if (!filePath) return;

    try {
        const res = await fetch(`/uploads/${encodeURIComponent(filePath)}`, { method: 'DELETE' });
        
        resultMessage.textContent = res.ok ? `Deleted "${filePath}"` : `Failed (status ${res.status})`;
        resultMessage.className = `result-message ${res.ok ? 'success' : 'error'}`;

        // Reload the current session's list (anonymous's files when logged out,
        // your own when logged in — the only files this session can delete).
        loadFileOptions('/my-uploads');
    } catch (error) {
        resultMessage.textContent = 'Error deleting file';
        resultMessage.className = 'result-message error';
    }
});

// Upload: submit via fetch and, on success, redirect to My Uploads
// (POST/Redirect/GET, done client-side since only the frontend is editable).
// The file is stored under the current session — anonymous when logged out —
// so /my-uploads always shows what was just uploaded.
const uploadForm = document.getElementById('upload-form');
uploadForm.addEventListener('submit', async (e) => {
    e.preventDefault();
    try {
        const res = await fetch('/uploads/', { method: 'POST', body: new FormData(uploadForm) });
        if (res.ok) {
            window.location.href = '/my-uploads';
        } else {
            resultMessage.textContent = `Upload failed (status ${res.status})`;
            resultMessage.className = 'result-message error';
        }
    } catch (error) {
        resultMessage.textContent = 'Error uploading file';
        resultMessage.className = 'result-message error';
    }
});

// Initialize Session State
(async function initSession() {
    const fallbackToGlobal = () => {
        showLoginBtn.classList.remove('hidden');
        panelTitle.textContent = '🌍 Anonymous Uploads';
        uploadButton.href = '/my-uploads';
        loadFileOptions('/my-uploads');
    };

    try {
        const res = await fetch('/session');
        const text = await res.text();
        const prefix = 'Logged in as ';
        
        if (!text.startsWith(prefix)) {
            fallbackToGlobal();
            return;
        }
        
        // HAS SESSION: Show personal files & Logout button
        const username = text.slice(prefix.length).split('\n')[0];
        logoutForm.classList.remove('hidden');
        userDisplay.textContent = `(${username})`;
        panelTitle.textContent = '📁 Upload files';
        
        // Dynamically pathing the href tag from the previous prompt
        uploadButton.href = '/my-uploads';
        uploadButton.textContent = `📁 My Uploads (${username})`;
        
        loadFileOptions('/my-uploads');
    } catch {
        fallbackToGlobal();
    }
})();
