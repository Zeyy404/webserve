#include "../include/SessionMiddleware.hpp"

const std::string SessionMiddleware::_cookieName = "SESSID";

SessionMiddleware::SessionMiddleware() {}

SessionMiddleware::SessionMiddleware(const SessionMiddleware& other) {
    (void)other;
}

SessionMiddleware::~SessionMiddleware() {}

SessionMiddleware& SessionMiddleware::operator=(const SessionMiddleware& other) {
    (void)other;
    return *this;
}

// Resolves the incoming Cookie header to a live Session and binds it to the
// request; falls back to creating a brand-new session when the cookie is
// absent, unknown, or points at an expired/purged session.
void SessionMiddleware::processRequest(HttpRequest& request) {
    SessionManager& manager = SessionManager::getInstance();
    CookieParser parser;
    std::string rawCookie = request.getHeader("Cookie");
    std::string sessionId = "";
    if (!rawCookie.empty()) {
        std::map<std::string, std::string> cookies = parser.parse(rawCookie);
        // Pick out our SESSID entry; other cookies in the header are ignored.
        std::map<std::string, std::string>::iterator it = cookies.find(_cookieName);
        if (it != cookies.end())
            sessionId = it->second;
    }
    if (!sessionId.empty()) {
        Session* session = manager.getSession(sessionId);
        if (session) {
            request.setSessionId(sessionId);
            request.setSession(session);
            return;
        }
    }
    std::string newSessionId = manager.createSession();
    Session* newSession = manager.getSession(newSessionId);
    request.setSessionId(newSessionId);
    request.setSession(newSession);
}

// Emits Set-Cookie only when the id now bound to the request differs from what
// the client sent (i.e. a session was freshly created), so unchanged sessions
// don't re-send the cookie on every response.
void SessionMiddleware::processResponse(HttpRequest& request, HttpResponse& response) {
    CookieParser parser;
    std::string rawCookie = request.getHeader("Cookie");
    std::string inComingSessionId;
    if (!rawCookie.empty()) {
        std::map<std::string, std::string> cookies = parser.parse(rawCookie);
        std::map<std::string, std::string>::iterator it = cookies.find(_cookieName);
        if (it != cookies.end())
            inComingSessionId = it->second;
    }
    if (inComingSessionId != request.getSessionId() && !request.getSessionId().empty())
        response.setCookieHeader(parser.build(_cookieName, request.getSessionId()));
}
