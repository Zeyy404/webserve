#ifndef SESSION_HPP
# define SESSION_HPP

#include <map>
#include <string>
#include <ctime>
#include <iomanip>
#include <cstdio>
#include <sstream>
#include <cstdlib>

// A single server-side session: an arbitrary string key/value store plus a
// last-accessed timestamp used by SessionManager to expire idle sessions.
class Session {

private:

    std::map<std::string, std::string>  _data;
    std::time_t                         _lastAccessed;

public:

    Session();
    Session(const Session& other);
    ~Session();

    Session& operator=(const Session& other);

    bool            hasKey(const std::string& key) const;
    std::string     getValue(const std::string& key) const;
    std::time_t     getLastAccessed() const;
    void            setData(const std::string& key, const std::string& value);
    void            unsetData(const std::string& key);
    void            touch();
    
};

// Process-wide singleton owning all live sessions, keyed by session id. It
// mints ids, hands out pointers into its own map, and lazily evicts sessions
// idle for longer than TTL. Not thread-safe: assumes the single-process,
// single-threaded event loop this server runs on.
class SessionManager {

private:
    
    std::map<std::string, Session> _sessions;
    static const int TTL = 300; // 5 minutes

    SessionManager();
    SessionManager(const SessionManager& other);
    SessionManager& operator=(const SessionManager& other);

    std::string generateSessionId();
    bool isExpired(const Session& session) const;

public:

    static SessionManager& getInstance();

    ~SessionManager();
    
    std::string     createSession();
    Session*        getSession(const std::string& id);
    void            destroySession(const std::string& sessionId);
    void            purgeExpiredSessions();
    
};

#endif