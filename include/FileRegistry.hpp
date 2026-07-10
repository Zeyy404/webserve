#ifndef FILEREGISTRY_HPP
# define FILEREGISTRY_HPP

# include <map>
# include <string>
# include <vector>

// Process-wide singleton mapping an upload owner (session username, or
// "anonymous" when not logged in) to that user's list of upload paths. Backs
// the /my-uploads view. Not thread-safe; suits the single-threaded event loop.
class FileRegistry {

private:

    std::map<std::string, std::vector<std::string> > _filesByUser;

    FileRegistry();
    FileRegistry(const FileRegistry& other);
    FileRegistry& operator=(const FileRegistry& other);

public:

    static FileRegistry& getInstance();

    ~FileRegistry();

    void                        loadFromDirectory(const std::string& dir);
    void                        registerFile(const std::string& username, const std::string& path);
    void                        registerFile(const std::string& path);
    void                        unregisterFile(const std::string& username, const std::string& path);
    void                        unregisterFile(const std::string& path);
    std::vector<std::string>    getFiles(const std::string& username) const;
    std::vector<std::string>    getFilesAndValidate(const std::string& username);
    bool                        hasFiles(const std::string& username) const;
    void                        clearUser(const std::string& username);
};

#endif
