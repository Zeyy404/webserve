var select = document.getElementById('filePath');
        var resultMessage = document.getElementById('resultMessage');
        
        // UI Elements
        var showLoginBtn = document.getElementById('show-login-btn');
        var logoutForm = document.getElementById('logout-form');
        var userDisplay = document.getElementById('user-display');
        var loginPanel = document.getElementById('login-panel');
        var filePanel = document.getElementById('file-panel');
        var panelTitle = document.getElementById('panel-title');
        var cancelLoginBtn = document.getElementById('cancel-login');

        // Toggle the login form
        showLoginBtn.addEventListener('click', function() {
            loginPanel.classList.remove('hidden');
            filePanel.classList.add('hidden');
        });

        cancelLoginBtn.addEventListener('click', function() {
            loginPanel.classList.add('hidden');
            filePanel.classList.remove('hidden');
        });

        // Load files depending on session state (global vs personal)
        function loadFileOptions(fetchUrl) {
            fetch(fetchUrl)
                .then(function (res) { return res.text(); })
                .then(function (html) {
                    var doc = new DOMParser().parseFromString(html, 'text/html');
                    select.innerHTML = '';
                    doc.querySelectorAll('a').forEach(function (a) {
                        var href = decodeURIComponent(a.getAttribute('href'));
                        // Strip leading paths to get just the filename
                        var name = href.replace(/^(\/?uploads\/?|\/?my-uploads\/?)/, '');
                        if (name && name !== '.' && name !== '..') {
                            var opt = document.createElement('option');
                            opt.value = name;
                            opt.textContent = name;
                            select.appendChild(opt);
                        }
                    });
                });
        }

        // Delete action
        document.getElementById('deleteButton').addEventListener('click', function () {
            var filePath = select.value;
            if (!filePath) return;

            // Assumes backend delete endpoint is always /uploads/{filename}
            fetch('/uploads/' + encodeURIComponent(filePath), { method: 'DELETE' })
                .then(function (res) {
                    resultMessage.textContent = res.ok
                        ? 'Deleted "' + filePath + '"'
                        : 'Failed (status ' + res.status + ')';
                    resultMessage.className = 'result-message' + (res.ok ? ' success' : ' error');
                    
                    // Reload the correct list based on if we are logged in
                    var isGlobal = panelTitle.textContent.indexOf('Global') !== -1;
                    loadFileOptions(isGlobal ? '/uploads/' : '/my-uploads');
                });
        });

        // Initialize Session State
        (function initSession() {
            fetch('/session')
                .then(function (res) { return res.text(); })
                .then(function (text) {
                    var prefix = 'Logged in as ';
                    if (text.indexOf(prefix) !== 0) {
                        // NO SESSION: Show global files & Login button
                        showLoginBtn.classList.remove('hidden');
                        panelTitle.textContent = '🌍 Global Uploads';
                        loadFileOptions('/uploads/');
                        return;
                    }
                    // HAS SESSION: Show personal files & Logout button
                    var username = text.slice(prefix.length).split('\n')[0];
                    logoutForm.classList.remove('hidden');
                    userDisplay.textContent = '(' + username + ')';
                    panelTitle.textContent = '📁 My Uploads (' + username + ')';
                    loadFileOptions('/my-uploads');
                })
                .catch(function() {
                    // Fallback to global state if session check fails
                    showLoginBtn.classList.remove('hidden');
                    panelTitle.textContent = '🌍 Global Uploads';
                    loadFileOptions('/uploads/');
                });
        })();