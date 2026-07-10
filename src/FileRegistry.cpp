#include "../include/FileRegistry.hpp"

#include <dirent.h>
#include <sys/stat.h>

FileRegistry::FileRegistry() {}

FileRegistry::~FileRegistry() {}

// Meyers singleton: the sole FileRegistry, constructed on first access.
FileRegistry& FileRegistry::getInstance() {
    static FileRegistry instance;
    return instance;
}

// Records path under username, ignoring empty args and de-duplicating so a
// re-upload of the same path doesn't list it twice.

void FileRegistry::registerFile(const std::string& username, const std::string& path) {
    if (username.empty() || path.empty())
        return;

    std::vector<std::string>& files = _filesByUser[username];
    for (size_t i = 0; i < files.size(); ++i) {
        if (files[i] == path)
            return;
    }
    files.push_back(path);
}

// Owner-less overload: attributes the upload to the shared "anonymous" bucket.
void FileRegistry::registerFile(const std::string& path) {
    registerFile("anonymous", path);
}

// Removes a single path from the user's list, dropping the user's entry
// entirely once their last file is gone so empty buckets don't linger.
void FileRegistry::unregisterFile(const std::string& username, const std::string& path) {
    std::map<std::string, std::vector<std::string> >::iterator it = _filesByUser.find(username);
    if (it == _filesByUser.end())
        return;

    std::vector<std::string>& files = it->second;
    for (std::vector<std::string>::iterator file = files.begin(); file != files.end(); ++file) {
        if (*file == path) {
            files.erase(file);
            break;
        }
    }
    if (files.empty())
        _filesByUser.erase(it);
}

void FileRegistry::unregisterFile(const std::string& path) {
    unregisterFile("anonymous", path);
}

// Returns the user's upload list, or an empty vector for an unknown user.
std::vector<std::string> FileRegistry::getFiles(const std::string& username) const {
    std::map<std::string, std::vector<std::string> >::const_iterator it = _filesByUser.find(username);
    if (it == _filesByUser.end())
        return std::vector<std::string>();
    return it->second;
}

bool FileRegistry::hasFiles(const std::string& username) const {
    std::map<std::string, std::vector<std::string> >::const_iterator it = _filesByUser.find(username);
    return it != _filesByUser.end() && !it->second.empty();
}

void FileRegistry::clearUser(const std::string& username) {
    _filesByUser.erase(username);
}

// Rebuilds the registry from files already on disk at startup, so uploads
// survive a restart. Skips dotfiles and non-regular entries, and infers the
// owner from the "<owner>~<name>" filename convention.
void FileRegistry::loadFromDirectory(const std::string& dir) {
    DIR* d = opendir(dir.c_str());
    if (d == NULL)
        return;
    struct dirent* entry;
    while ((entry = readdir(d)) != NULL) {
        std::string name(entry->d_name);
        if (name.empty() || name[0] == '.')
            continue;
        std::string full = dir + (dir[dir.size() - 1] == '/' ? "" : "/") + name;
        struct stat st;
        if (::stat(full.c_str(), &st) != 0 || !S_ISREG(st.st_mode))
            continue;
        // Text before the first '~' is the owner; no '~' means an anonymous upload.
        size_t tilde = name.find('~');
        std::string owner = tilde == std::string::npos ? "anonymous" : name.substr(0, tilde);
        registerFile(owner, "/uploads/" + name);
    }
    closedir(d);
}
